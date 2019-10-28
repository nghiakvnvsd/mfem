#include "navier_solver.hpp"
#include <fstream>

using namespace mfem;
using namespace navier;

struct s_NavierContext
{
   int order = 7;
   double kin_vis = 1.0 / 1600.0;
   double t_final = 10e-3;
   double dt = 1e-3;
   bool pa = false;
   bool ni = false;
} ctx;

void vel_tgv(const Vector &x, double t, Vector &u)
{
   double xi = x(0);
   double yi = x(1);
   double zi = x(2);

   u(0) = sin(xi) * cos(yi) * cos(zi);
   u(1) = -cos(xi) * sin(yi) * cos(zi);
   u(2) = 0.0;
}

class QOI
{
public:
   QOI(ParMesh *pmesh)
   {
      H1_FECollection h1fec(1);
      ParFiniteElementSpace h1fes(pmesh, &h1fec);

      onecoeff.constant = 1.0;
      mass_lf = new ParLinearForm(&h1fes);
      mass_lf->AddDomainIntegrator(new DomainLFIntegrator(onecoeff));
      mass_lf->Assemble();

      ParGridFunction one_gf(&h1fes);
      one_gf.ProjectCoefficient(onecoeff);

      volume = mass_lf->operator()(one_gf);
   };

   double ComputeKineticEnergy(ParGridFunction &v)
   {
      Vector velx, vely, velz;
      double integ = 0.0;
      const FiniteElement *fe;
      ElementTransformation *T;
      FiniteElementSpace *fes = v.FESpace();

      for (int i = 0; i < fes->GetNE(); i++)
      {
         fe = fes->GetFE(i);
         double intorder = 2 * fe->GetOrder();
         const IntegrationRule *ir = &(
            IntRules.Get(fe->GetGeomType(), intorder));

         v.GetValues(i, *ir, velx, 1);
         v.GetValues(i, *ir, vely, 2);
         v.GetValues(i, *ir, velz, 3);

         T = fes->GetElementTransformation(i);
         for (int j = 0; j < ir->GetNPoints(); j++)
         {
            const IntegrationPoint &ip = ir->IntPoint(j);
            T->SetIntPoint(&ip);

            double vel2 = velx(j) * velx(j) + vely(j) * vely(j)
                          + velz(j) * velz(j);

            integ += ip.weight * T->Weight() * vel2;
         }
      }

      double global_integral = 0.0;
      MPI_Allreduce(&integ,
                    &global_integral,
                    1,
                    MPI_DOUBLE,
                    MPI_SUM,
                    MPI_COMM_WORLD);

      return 0.5 * global_integral / volume;
   };

   ~QOI() { delete mass_lf; };

private:
   ConstantCoefficient onecoeff;
   ParLinearForm *mass_lf;
   double volume;
};

template<typename T>
T sq(T x)
{
   return x * x;
}

void ComputeQCriterion(ParGridFunction &u, ParGridFunction &q)
{
   FiniteElementSpace *v_fes = u.FESpace();
   FiniteElementSpace *fes = q.FESpace();

   // AccumulateAndCountZones
   Array<int> zones_per_vdof;
   zones_per_vdof.SetSize(fes->GetVSize());
   zones_per_vdof = 0;

   q = 0.0;

   // Local interpolation
   int elndofs;
   Array<int> v_dofs, dofs;
   Vector vals;
   Vector loc_data;
   int vdim = v_fes->GetVDim();
   DenseMatrix grad_hat;
   DenseMatrix dshape;
   DenseMatrix grad;

   for (int e = 0; e < fes->GetNE(); ++e)
   {
      fes->GetElementVDofs(e, dofs);
      v_fes->GetElementVDofs(e, v_dofs);
      u.GetSubVector(v_dofs, loc_data);
      vals.SetSize(dofs.Size());
      ElementTransformation *tr = fes->GetElementTransformation(e);
      const FiniteElement *el = fes->GetFE(e);
      elndofs = el->GetDof();
      int dim = el->GetDim();
      dshape.SetSize(elndofs, dim);

      for (int dof = 0; dof < elndofs; ++dof)
      {
         // Project
         const IntegrationPoint &ip = el->GetNodes().IntPoint(dof);
         tr->SetIntPoint(&ip);

         // Eval
         // GetVectorGradientHat
         el->CalcDShape(tr->GetIntPoint(), dshape);
         grad_hat.SetSize(vdim, dim);
         DenseMatrix loc_data_mat(loc_data.GetData(), elndofs, vdim);
         MultAtB(loc_data_mat, dshape, grad_hat);

         const DenseMatrix &Jinv = tr->InverseJacobian();
         grad.SetSize(grad_hat.Height(), Jinv.Width());
         Mult(grad_hat, Jinv, grad);

         double q_val = 0.5 * (sq(grad(0, 0)) + sq(grad(1, 1)) + sq(grad(2, 2)))
                        + grad(0, 1) * grad(1, 0) + grad(0, 2) * grad(2, 0)
                        + grad(1, 2) * grad(2, 1);

         vals(dof) = q_val;
      }

      // Accumulate values in all dofs, count the zones.
      for (int j = 0; j < dofs.Size(); j++)
      {
         int ldof = dofs[j];
         q(ldof) += vals[j];
         zones_per_vdof[ldof]++;
      }
   }

   // Communication

   // Count the zones globally.
   GroupCommunicator &gcomm = q.ParFESpace()->GroupComm();
   gcomm.Reduce<int>(zones_per_vdof, GroupCommunicator::Sum);
   gcomm.Bcast(zones_per_vdof);

   // Accumulate for all vdofs.
   gcomm.Reduce<double>(q.GetData(), GroupCommunicator::Sum);
   gcomm.Bcast<double>(q.GetData());

   // Compute means
   for (int i = 0; i < q.Size(); i++)
   {
      const int nz = zones_per_vdof[i];
      if (nz)
      {
         q(i) /= nz;
      }
   }
}

int main(int argc, char *argv[])
{
   MPI_Session mpi(argc, argv);

   int ser_ref_levels = 1;

   OptionsParser args(argc, argv);
   args.AddOption(&ser_ref_levels,
                  "-rs",
                  "--refine-serial",
                  "Number of times to refine the mesh uniformly in serial.");
   args.AddOption(&ctx.order,
                  "-o",
                  "--order",
                  "Order (degree) of the finite elements.");
   args.AddOption(&ctx.dt, "-dt", "--time-step", "Time step.");
   args.AddOption(&ctx.t_final, "-tf", "--final-time", "Final time.");
   args.AddOption(&ctx.pa,
                  "-pa",
                  "--enable-pa",
                  "-no-pi",
                  "--disable-pi",
                  "Enable partial assembly.");
   args.AddOption(&ctx.ni,
                  "-ni",
                  "--enable-ni",
                  "-no-ni",
                  "--disable-ni",
                  "Enable numerical integration rules.");
   args.Parse();
   if (!args.Good())
   {
      if (mpi.Root())
      {
         args.PrintUsage(std::cout);
      }
      MPI_Finalize();
      return 1;
   }
   if (mpi.Root())
   {
      args.PrintOptions(std::cout);
   }

   Mesh *orig_mesh = new Mesh("../../data/periodic-cube.mesh");
   Mesh *mesh = new Mesh(orig_mesh, ser_ref_levels, BasisType::ClosedUniform);
   delete orig_mesh;

   mesh->EnsureNodes();
   GridFunction *nodes = mesh->GetNodes();
   *nodes *= M_PI;

   int nel = mesh->GetNE();
   if (mpi.Root())
   {
      std::cout << "Number of elements: " << nel << std::endl;
   }

   ParMesh *pmesh = new ParMesh(MPI_COMM_WORLD, *mesh);
   delete mesh;

   // Create the flow solver.
   NavierSolver naviersolver(pmesh, ctx.order, ctx.kin_vis);
   naviersolver.EnablePA(ctx.pa);
   naviersolver.EnableNI(ctx.ni);

   // Set the initial condition.
   // This is completely user customizeable.
   ParGridFunction *u_ic = naviersolver.GetCurrentVelocity();
   VectorFunctionCoefficient u_excoeff(pmesh->Dimension(), vel_tgv);
   u_ic->ProjectCoefficient(u_excoeff);

   double t = 0.0;
   double dt = ctx.dt;
   double t_final = ctx.t_final;
   bool last_step = false;

   naviersolver.Setup(dt);

   ParGridFunction *u_gf = naviersolver.GetCurrentVelocity();
   ParGridFunction *p_gf = naviersolver.GetCurrentPressure();

   ParGridFunction w_gf(*u_gf);
   ParGridFunction q_gf(*p_gf);
   naviersolver.ComputeCurl3D(*u_gf, w_gf);
   ComputeQCriterion(*u_gf, q_gf);

   QOI kin_energy(pmesh);

   VisItDataCollection visit_dc("ins", pmesh);
   visit_dc.SetPrefixPath("output");
   visit_dc.SetCycle(0);
   visit_dc.SetTime(t);
   visit_dc.RegisterField("velocity", u_gf);
   visit_dc.RegisterField("pressure", p_gf);
   visit_dc.RegisterField("vorticity", &w_gf);
   visit_dc.RegisterField("qcriterion", &q_gf);
   visit_dc.Save();

   std::ofstream ofs0("output/qcrit.gf");
   q_gf.Save(ofs0);
   ofs0.close();
   std::ofstream ofs1("output/mesh");
   pmesh->Print(ofs1);
   ofs1.close();

   double u_inf_loc = u_gf->Normlinf();
   double p_inf_loc = p_gf->Normlinf();
   double u_inf = GlobalLpNorm(infinity(), u_inf_loc, MPI_COMM_WORLD);
   double p_inf = GlobalLpNorm(infinity(), p_inf_loc, MPI_COMM_WORLD);
   double ke = kin_energy.ComputeKineticEnergy(*u_gf);

   std::string fname = "tgv_out_p_" + std::to_string(ctx.order) + ".txt";
   FILE *f;

   if (mpi.Root())
   {
      int nel1d = std::round(pow(nel, 1.0 / 3.0));
      int ngridpts = p_gf->ParFESpace()->GlobalVSize();
      printf("%.5E %.5E %.5E %.5E %.5E\n", t, dt, u_inf, p_inf, ke);

      f = fopen(fname.c_str(), "w");
      fprintf(f, "3D Taylor Green Vortex\n");
      fprintf(f, "order = %d\n", ctx.order);
      fprintf(f, "grid = %d x %d x %d\n", nel1d, nel1d, nel1d);
      fprintf(f, "dofs per component = %d\n", ngridpts);
      fprintf(f, "=================================================\n");
      fprintf(f, "        time                   kinetic energy\n");
      fprintf(f, "%20.16e     %20.16e\n", t, ke);
      fflush(f);
      fflush(stdout);
   }

   for (int step = 0; !last_step; ++step)
   {
      if (t + dt >= t_final - dt / 2)
      {
         last_step = true;
      }

      naviersolver.Step(t, dt, step);

      if ((step + 1) % 100 == 0 || last_step)
      {
         naviersolver.ComputeCurl3D(*u_gf, w_gf);
         ComputeQCriterion(*u_gf, q_gf);
         visit_dc.SetCycle(step);
         visit_dc.SetTime(t);
         visit_dc.Save();
      }

      double u_inf_loc = u_gf->Normlinf();
      double p_inf_loc = p_gf->Normlinf();
      double u_inf = GlobalLpNorm(infinity(), u_inf_loc, MPI_COMM_WORLD);
      double p_inf = GlobalLpNorm(infinity(), p_inf_loc, MPI_COMM_WORLD);
      double ke = kin_energy.ComputeKineticEnergy(*u_gf);
      if (mpi.Root())
      {
         printf("%.5E %.5E %.5E %.5E %.5E\n", t, dt, u_inf, p_inf, ke);
         fprintf(f, "%20.16e     %20.16e\n", t, ke);
         fflush(f);
         fflush(stdout);
      }
   }

   naviersolver.PrintTimingData();

   delete pmesh;

   return 0;
}