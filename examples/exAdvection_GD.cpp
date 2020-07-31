#include "mfem.hpp"
#include "exAdvection.hpp"
#include "exGD.hpp"
#include "centgridfunc.hpp"
#include <fstream>
#include <iostream>
using namespace std;
using namespace mfem;
double u_exact(const Vector &);
double f_exact(const Vector &);
// Velocity coefficient
void velocity_function(const Vector &x, Vector &v);
void exact_function(const Vector &x, Vector &v);

int main(int argc, char *argv[])
{
   int ref_levels = -1;
   int order = 1;
   bool visualization = 1;
   double scale;
   int cutsize = 1;
   int N = 20;
   /// parse the options
   OptionsParser args(argc, argv);
   args.AddOption(&order, "-o", "--order",
                  "Order (degree) of the finite elements.");
   args.AddOption(&cutsize, "-s", "--cutsize",
                  "scale of the cut finite elements.");
   args.AddOption(&N, "-n", "--#elements",
                  "number of mesh elements.");
   args.Parse();
   if (!args.Good())
   {
      args.PrintUsage(cout);
      return 1;
   }
   args.PrintOptions(cout);
   /// mesh to be used
   Mesh *mesh = new Mesh(N, 1);
   int dim = mesh->Dimension();
   cout << "number of elements " << mesh->GetNE() << endl;
   ofstream sol_ofv("square_disc_mesh.vtk");
   sol_ofv.precision(14);
   mesh->PrintVTK(sol_ofv, 1);
   int nels = mesh->GetNE();
   scale = 1.0 / nels;
   scale = scale/cutsize;
   // 4. Define a finite element space on the mesh. Here we use discontinuous
   //    finite elements of the specified order >= 0.
   FiniteElementCollection *fec = new DG_FECollection(order, dim);
   FiniteElementSpace *fes = new FiniteElementSpace(mesh, fec);
   /// GD finite element space
   FiniteElementSpace *fespace = new GalerkinDifference(mesh, dim, mesh->GetNE(), fec, scale, 1, Ordering::byVDIM, order);
   cout << "Number of unknowns: " << fespace->GetTrueVSize() << endl;
   cout << "#dofs " << fespace->GetNDofs() << endl;
   // 5. Set up the linear form b(.) which corresponds to the right-hand side of
   //    the FEM linear system.
   CentGridFunction y(fespace);
   y = 0.0;
   cout << "center grid function created " << endl;
   VectorFunctionCoefficient exact(dim, exact_function);
   y.ProjectCoefficient(exact);
   cout << "solution at center is " << endl;
   y.Print();
   GridFunction x(fes);
   LinearForm *b = new LinearForm(fes);
   ConstantCoefficient one(-1.0);
   ConstantCoefficient zero(0.0);
   FunctionCoefficient f(f_exact);
   FunctionCoefficient u(u_exact);
   VectorFunctionCoefficient velocity(dim, velocity_function);
   b->AddDomainIntegrator(new CutDomainLFIntegrator(f, scale, nels));
   b->AddBdrFaceIntegrator(new BoundaryAdvectIntegrator(u, velocity, -1.0, -0.5, nels, scale));
   b->Assemble();
   BilinearForm *a = new BilinearForm(fes);
   a->AddDomainIntegrator(new AdvectionIntegrator(velocity, scale, nels, -1.0));
   a->AddInteriorFaceIntegrator(new DGFaceIntegrator(velocity, 1.0, -0.5, scale, nels));
   a->AddBdrFaceIntegrator(new DGFaceIntegrator(velocity, 1.0, -0.5, scale, nels));
   a->Assemble();
   a->Finalize();
   SparseMatrix &Aold = a->SpMat();
   SparseMatrix *cp = dynamic_cast<GalerkinDifference *>(fespace)->GetCP();
   SparseMatrix *p = RAP(*cp, Aold, *cp);
   SparseMatrix &A = *p;

   // fespace->GetProlongationMatrix()->Mult(y, x);
   // GridFunction Px(fes);
   // cp->Mult(y,Px);
   // cout << "Px is " << endl;
   // Px.Print();
   // cout << "x is " << endl;
   // x.Print();
   Vector bnew(A.Width());
   fespace->GetProlongationMatrix()->MultTranspose(*b, bnew);
   ofstream write("stiffmat_GD.txt");
   A.PrintMatlab(write);
   write.close(); 
#ifndef MFEM_USE_SUITESPARSE
   // 8. Define a simple symmetric Gauss-Seidel preconditioner and use it to
   //    solve the system Ax=b with PCG in the symmetric case, and GMRES in the
   //    non-symmetric one.
   GSSmoother M(A);
   //PCG(A, M, *b, x, 1, 1000, 1e-12, 0.0);
   // else
   // {
   GMRES(A, M, *b, x, 1, 1000, 200, 1e-60, 1e-60);
   // }
#else
   // 8. If MFEM was compiled with SuiteSparse, use UMFPACK to solve the system.
   UMFPackSolver umf_solver;
   umf_solver.Control[UMFPACK_ORDERING] = UMFPACK_ORDERING_METIS;
   umf_solver.SetOperator(A);
   umf_solver.Mult(bnew, y);
#endif
   cout << "----------------------------- "<< endl;
   cout << "solution at center obtained: "<< endl;
   y.Print();
   cout << "----------------------------- "<< endl;
   fespace->GetProlongationMatrix()->Mult(y, x);
   // Vector residual(Aold.Width());
   // Aold.Mult(x, residual);
   // residual -= *b;
   // residual.Print();
   // Vector cpres(A.Width());
   // cp->MultTranspose(residual, cpres);
   // cout << "cpres " << endl;
   // cpres.Print(); 
   ofstream adj_ofs("dgAdvection_GD.vtk");
   adj_ofs.precision(14);
   mesh->PrintVTK(adj_ofs, 1);
   x.SaveVTK(adj_ofs, "dgAdvSolution_GD", 1);
   adj_ofs.close();
   double norm = CutComputeL2Error(x, fes, u, scale);
   cout << "mesh size, h = " << 1.0 / mesh->GetNE() << endl;
   cout << "solution norm: " << norm << endl;
   //cout << x.ComputeL2Error(u) << endl;
   cout << "solution at nodes is: " << endl;
   x.Print();
   // 11. Free the used memory.
   delete a;
   delete b;
   delete fespace;
   delete fes;
   delete fec;
   delete mesh;
   return 0;
}

void exact_function(const Vector &x, Vector &v)
{
   int dim = x.Size();
  // v(0) = x(0)*x(0);
   v(0) = exp(x(0));
   //v(0) = x(0);
}

// Velocity coefficient
double u_exact(const Vector &x)
{
   return exp(x(0));
   //return x(0) ;
   //return x(0)*x(0);
}
double f_exact(const Vector &x)
{
   return exp(x(0));
   //return -1.0;
   //return -2.0*x(0);
}
// Velocity coefficient
void velocity_function(const Vector &x, Vector &v)
{
   int dim = x.Size();
   switch (dim)
   {
   case 1:
      v(0) = -1.0;
      break;
   case 2:
      v(0) = sqrt(2. / 3.);
      v(1) = sqrt(1. / 3.);
      break;
   case 3:
      v(0) = sqrt(3. / 6.);
      v(1) = sqrt(2. / 6.);
      v(2) = sqrt(1. / 6.);
      break;
   }
}

/// function to compute l2 error for cut domain
double CutComputeL2Error(GridFunction &x, FiniteElementSpace *fes,
                         Coefficient &exsol, double scale)
{
   double error = 0.0;
   const FiniteElement *fe;
   ElementTransformation *T;
   Vector vals;
   Vector quad_coord(1);
   int p = 2;
   for (int i = 0; i < fes->GetNE(); i++)
   {
      fe = fes->GetFE(i);
      const IntegrationRule *ir;
      int intorder = 2 * fe->GetOrder() + 1; // <----------
      ir = &(IntRules.Get(fe->GetGeomType(), intorder));
      T = fes->GetElementTransformation(i);
      if (T->ElementNo == fes->GetNE() - 1)
      {   cout << "***************************************** " << endl;
          cout << "Element " << i << endl;
          int vdim=1;
          //cout << "element is " << i << endl;
         IntegrationRule *cutir;
         cutir = new IntegrationRule(ir->Size());
         for (int k = 0; k < cutir->GetNPoints(); k++)
         {
            IntegrationPoint &cutip = cutir->IntPoint(k);
            const IntegrationPoint &ip = ir->IntPoint(k);
            cutip.x = (scale * ip.x) / T->Weight();
            cutip.weight = ip.weight;
         }
         Array<int> dofs;
         int n = ir->GetNPoints();
         vals.SetSize(n);
         fes->GetElementDofs(i, dofs);
         fes->DofsToVDofs(vdim - 1, dofs);
         const FiniteElement *FElem = fes->GetFE(i);
         MFEM_ASSERT(FElem->GetMapType() == FiniteElement::VALUE,
                     "invalid FE map type");
         int dof = FElem->GetDof();
         Vector DofVal(dof), loc_data(dof);
         x.GetSubVector(dofs, loc_data);
         // not used for error calculation
         x.GetValues(i, *cutir, vals);
         for (int j = 0; j < cutir->GetNPoints(); j++)
         {
            IntegrationPoint &ip = cutir->IntPoint(j);
            T->SetIntPoint(&ip);
            cout << "int point is " << ip.x << endl;
            cout << "u is " << exsol.Eval(*T, ip) << endl;
            cout << "x is " << fabs(vals(j)) << endl;
            cout << "xq is " << loc_data(j) << endl;
            double err = (fabs(vals(j)) - exsol.Eval(*T, ip));
            //double err = (loc_data(j) - exsol.Eval(*T, ip));
            if (p < infinity())
            {
               err = pow(err, p);
               error += ip.weight * scale * err;
            }
            else
            {
               error = std::max(error, err);
            }
         }
          cout << "***************************************** " << endl;
      }
      else
      {
         x.GetValues(i, *ir, vals);
         for (int j = 0; j < ir->GetNPoints(); j++)
         {
            const IntegrationPoint &ip = ir->IntPoint(j);
            T->SetIntPoint(&ip);
            double err = fabs(vals(j) - exsol.Eval(*T, ip));
            if (p < infinity())
            {
               err = pow(err, p);
               error += ip.weight * T->Weight() * err;
            }
            else
            {
               error = std::max(error, err);
            }
         }
      }
   }
   if (p < infinity())
   {
      // negative quadrature weights may cause the error to be negative
      if (error < 0.)
      {
         error = -pow(-error, 1. / p);
      }
      else
      {
         error = pow(error, 1. / p);
      }
   }

   return error;
}

void CutDomainLFIntegrator::AssembleRHSElementVect(const FiniteElement &el,
                                                   ElementTransformation &Tr,
                                                   Vector &elvect)
{
   int dof = el.GetDof();
   double sf;
   shape.SetSize(dof); // vector of size dof
   elvect.SetSize(dof);
   elvect = 0.0;
   const IntegrationRule *ir = IntRule;
   if (ir == NULL)
   {
      // ir = &IntRules.Get(el.GetGeomType(),
      //                    oa * el.GetOrder() + ob + Tr.OrderW());
      ir = &IntRules.Get(el.GetGeomType(), oa * el.GetOrder() + ob);
      // int order = Tr.OrderGrad(&el) + Tr.Order() + el.GetOrder();
      // ir = &IntRules.Get(el.GetGeomType(), order);
   }
   IntegrationRule *cutir;
   cutir = new IntegrationRule(ir->Size());
   if (Tr.ElementNo == nels - 1)
   {
      for (int k = 0; k < cutir->GetNPoints(); k++)
      {
         IntegrationPoint &cutip = cutir->IntPoint(k);
         const IntegrationPoint &ip = ir->IntPoint(k);
         cutip.x = (scale * ip.x) / Tr.Weight();
         cutip.weight = ip.weight;
      }
   }
   for (int i = 0; i < ir->GetNPoints(); i++)
   {
      const IntegrationPoint &ip = ir->IntPoint(i);
      IntegrationPoint &cutip = cutir->IntPoint(i);
      double val;
      if (Tr.ElementNo == nels - 1)
      {
         Tr.SetIntPoint(&cutip);
         val = scale * Q.Eval(Tr, cutip);
         el.CalcShape(cutip, shape);
      }
      else
      {
         Tr.SetIntPoint(&ip);
         val = Tr.Weight() * Q.Eval(Tr, ip);
         el.CalcShape(ip, shape);
      }
      add(elvect, ip.weight * val, shape, elvect);
   }
}

void CutDomainLFIntegrator::AssembleDeltaElementVect(
    const FiniteElement &fe, ElementTransformation &Trans, Vector &elvect)
{
   MFEM_ASSERT(delta != NULL, "coefficient must be DeltaCoefficient");
   elvect.SetSize(fe.GetDof());
   fe.CalcPhysShape(Trans, elvect);
   elvect *= delta->EvalDelta(Trans, Trans.GetIntPoint());
}

void AdvectionIntegrator::AssembleElementMatrix(
    const FiniteElement &el, ElementTransformation &Trans, DenseMatrix &elmat)
{
   int nd = el.GetDof();
   int dim = el.GetDim();
   double w;
#ifdef MFEM_THREAD_SAFE
   DenseMatrix dshape, adjJ, Q_ir;
   Vector shape, vec2, BdFidxT;
#endif
   elmat.SetSize(nd);
   dshape.SetSize(nd, dim);
   adjJ.SetSize(dim);
   shape.SetSize(nd);
   vec2.SetSize(dim);
   BdFidxT.SetSize(nd);
   Vector vec1;
   const IntegrationRule *ir = IntRule;
   if (ir == NULL)
   {
      int order = Trans.OrderGrad(&el) + Trans.Order() + el.GetOrder();
      ir = &IntRules.Get(el.GetGeomType(), order);
   }
   if (Trans.ElementNo == nels - 1)
   {
      IntegrationRule *cutir;
      cutir = new IntegrationRule(ir->Size());
      for (int k = 0; k < cutir->GetNPoints(); k++)
      {
         IntegrationPoint &cutip = cutir->IntPoint(k);
         const IntegrationPoint &ip = ir->IntPoint(k);
         cutip.x = (scale * ip.x) / Trans.Weight();
         cutip.weight = ip.weight;
      }
      Q->Eval(Q_ir, Trans, *cutir);
      elmat = 0.0;
      for (int i = 0; i < cutir->GetNPoints(); i++)
      {
         IntegrationPoint &ip = cutir->IntPoint(i);
         el.CalcDShape(ip, dshape);
         el.CalcShape(ip, shape);
         Trans.SetIntPoint(&ip);
         CalcAdjugate(Trans.Jacobian(), adjJ);
         adjJ *= scale / Trans.Weight();
         Q_ir.GetColumnReference(i, vec1);
         vec1 *= alpha * ip.weight;
         adjJ.Mult(vec1, vec2);
         dshape.Mult(vec2, BdFidxT);
         AddMultVWt(shape, BdFidxT, elmat);
      }
   }
   else
   {
      Q->Eval(Q_ir, Trans, *ir);
      elmat = 0.0;
      for (int i = 0; i < ir->GetNPoints(); i++)
      {
         const IntegrationPoint &ip = ir->IntPoint(i);
         el.CalcDShape(ip, dshape);
         el.CalcShape(ip, shape);
         Trans.SetIntPoint(&ip);
         CalcAdjugate(Trans.Jacobian(), adjJ);
         adjJ *= 1;
         Q_ir.GetColumnReference(i, vec1);
         vec1 *= alpha * ip.weight;
         adjJ.Mult(vec1, vec2);
         dshape.Mult(vec2, BdFidxT);
         AddMultVWt(shape, BdFidxT, elmat);
      }
   }
}

// assemble the elmat for interior and boundary faces
void DGFaceIntegrator::AssembleFaceMatrix(const FiniteElement &el1,
                                          const FiniteElement &el2,
                                          FaceElementTransformations &Trans,
                                          DenseMatrix &elmat)
{
   int dim, ndof1, ndof2;
   double un, a, b, w;
   dim = el1.GetDim();
   ndof1 = el1.GetDof();
   Vector vu(dim), nor(dim);
   if (Trans.Elem2No >= 0)
   {
      ndof2 = el2.GetDof();
      shape2.SetSize(ndof2);
   }
   else
   {
      ndof2 = 0;
   }
   shape1.SetSize(ndof1);
   elmat.SetSize(ndof1 + ndof2);
   elmat = 0.0;
   const IntegrationRule *ir = IntRule;
   if (ir == NULL)
   {
      int order;
      // Assuming order(u)==order(mesh)
      if (Trans.Elem2No >= 0)
         order = (min(Trans.Elem1->OrderW(), Trans.Elem2->OrderW()) +
                  2 * max(el1.GetOrder(), el2.GetOrder()));
      else
      {
         order = Trans.Elem1->OrderW() + 2 * el1.GetOrder();
      }
      if (el1.Space() == FunctionSpace::Pk)
      {
         order++;
      }
      ir = &IntRules.Get(Trans.FaceGeom, order);
   }
   // IntegrationRule *cutir;
   // if (Trans.Elem1No == nels - 1)
   // {
   //    cutir = new IntegrationRule(ir->Size());
   //    for (int k = 0; k < cutir->GetNPoints(); k++)
   //    {
   //       IntegrationPoint &cutip = cutir->IntPoint(k);
   //       const IntegrationPoint &ip = ir->IntPoint(k);
   //       cout << "without scaling " << ip.x << endl;
   //       cutip.x = (scale * ip.x) / Trans.Elem1->Weight();
   //       cutip.weight = ip.weight;
   //       cout << "rule " << cutip.x << endl;
   //    }
   // }
   for (int p = 0; p < ir->GetNPoints(); p++)
   {
      const IntegrationPoint &ip = ir->IntPoint(p);
      // if (Trans.Elem1No == nels - 1)
      // {
      //    const IntegrationPoint &ip = cutir->IntPoint(p);
      // }
      IntegrationPoint eip1, eip2;
      Trans.Loc1.Transform(ip, eip1);
      if (Trans.Elem1No == nels - 1)
      {
         eip1.x = (scale * eip1.x) / Trans.Elem1->Weight();
      }
      if (ndof2)
      {
         Trans.Loc2.Transform(ip, eip2);
         if (Trans.Elem2No == nels - 1)
         {
            eip2.x = (scale * eip2.x) / Trans.Elem2->Weight();
         }
      }

      el1.CalcShape(eip1, shape1);
      Trans.Face->SetIntPoint(&ip);
      Trans.Elem1->SetIntPoint(&eip1);
      u->Eval(vu, *Trans.Elem1, eip1);
      nor(0) = 2 * eip1.x - 1.0;
      if (Trans.Elem1No == nels - 1)
      {
         nor(0) = 1.0;
      }
      un = vu * nor;
      a = 0.5 * alpha * un;
      b = beta * fabs(un);
      w = ip.weight * (a + b);
      if (ndof2)
      {
         w /= 2;
      }
      if (w != 0.0)
      {
         for (int i = 0; i < ndof1; i++)
            for (int j = 0; j < ndof1; j++)
            {
               elmat(j, i) += w * shape1(i) * shape1(j);
            }
      }
      if (ndof2)
      {
         el2.CalcShape(eip2, shape2);
         if (w != 0.0)
            for (int i = 0; i < ndof2; i++)
               for (int j = 0; j < ndof1; j++)
               {
                  elmat(j, ndof1 + i) -= w * shape2(i) * shape1(j);
               }
         w = (ip.weight * (b - a));
         if (w != 0.0)
         {
            for (int i = 0; i < ndof2; i++)
               for (int j = 0; j < ndof2; j++)
               {
                  elmat(ndof1 + j, ndof1 + i) += w * shape2(i) * shape2(j);
               }

            for (int i = 0; i < ndof1; i++)
               for (int j = 0; j < ndof2; j++)
               {
                  elmat(ndof1 + j, i) -= w * shape1(i) * shape2(j);
               }
         }
      }
   }
}

void BoundaryAdvectIntegrator::AssembleRHSElementVect(
    const FiniteElement &el, FaceElementTransformations &Tr, Vector &elvect)
{
   int dim, ndof, order;
   double un, w, vu_data[3], nor_data[3];
   dim = el.GetDim();
   ndof = el.GetDof();
   elvect.SetSize(ndof);
   // if (Tr.Face->ElementNo == nels - 1)
   // {
   //    elvect = 0.0;
   // }
   if (Tr.Face->ElementNo == 0)
   {
      elvect = 0.0;
   }
   else
   {
      Vector vu(vu_data, dim), nor(nor_data, dim);

      shape.SetSize(ndof);
      elvect = 0.0;
      const IntegrationRule *ir = IntRule;
      if (ir == NULL)
      {
         // Assuming order(u)==order(mesh)
         order = Tr.Elem1->OrderW() + 2 * el.GetOrder();
         if (el.Space() == FunctionSpace::Pk)
         {
            order++;
         }
         ir = &IntRules.Get(Tr.FaceGeom, order);
      }
      for (int p = 0; p < ir->GetNPoints(); p++)
      {
         const IntegrationPoint &ip = ir->IntPoint(p);
         IntegrationPoint eip;
         Tr.Loc1.Transform(ip, eip);
         eip.x = (scale * eip.x) / Tr.Elem1->Weight();
         el.CalcShape(eip, shape);
         Tr.Face->SetIntPoint(&ip);
         u->Eval(vu, *Tr.Elem1, eip);
         //nor(0) = 2 * eip.x - 1.0;
         nor(0) = 1;
         un = vu * nor;
         w = -0.5 * alpha * un + beta * fabs(un);
         w *= ip.weight * uD->Eval(*Tr.Elem1, eip);
         elvect.Add(w, shape);
      }
   }
}

void BoundaryAdvectIntegrator::AssembleRHSElementVect(
    const FiniteElement &el, ElementTransformation &Tr, Vector &elvect)
{
   mfem_error("BoundaryFlowIntegrator::AssembleRHSElementVect\n"
              "  is not implemented as boundary integrator!\n"
              "  Use LinearForm::AddBdrFaceIntegrator instead of\n"
              "  LinearForm::AddBoundaryIntegrator.");
}

/// functions for `GalerkinDifference` class

void GalerkinDifference::BuildNeighbourMat(const mfem::Array<int> &elmt_id,
                                           mfem::DenseMatrix &mat_cent,
                                           mfem::DenseMatrix &mat_quad) const
{
   // resize the DenseMatrices and clean the data
   int num_el = elmt_id.Size();
   mat_cent.Clear();
   mat_cent.SetSize(dim, num_el);

   const FiniteElement *fe = fec->FiniteElementForGeometry(Geometry::SEGMENT);
   const int num_dofs = fe->GetDof();
   // vectors that hold coordinates of quadrature points
   // used for duplication tests
   vector<double> quad_data;
   Vector quad_coord(dim); // used to store quadrature coordinate temperally
   ElementTransformation *eltransf;
   for (int j = 0; j < num_el; j++)
   {
      // Get and store the element center
      mfem::Vector cent_coord(dim);
      GetElementCenter(elmt_id[j], cent_coord);
      for (int i = 0; i < dim; i++)
      {
         mat_cent(i, j) = cent_coord(i);
      }
      // deal with quadrature points
      eltransf = mesh->GetElementTransformation(elmt_id[j]);
      cout << " element " << elmt_id[j] << " quadrature points are " << endl;
      for (int k = 0; k < num_dofs; k++)
      {
         const IntegrationPoint &eip = fe->GetNodes().IntPoint(k);
         //eip.x= (scale * eip.x) / eltransf->Weight();
         eltransf->Transform(eip, quad_coord);
         // if (eltransf->ElementNo == nEle - 1)
         // {
         //    quad_coord(0) = (eip.x * scale) + 
         //               (eltransf->ElementNo * eltransf->Weight());
         // }
         // cout << quad_coord(0) << endl;
         for (int di = 0; di < dim; di++)
         {
            quad_data.push_back(quad_coord(di));
         }
      }
   }
   // reset the quadrature point matrix
   mat_quad.Clear();
   int num_col = quad_data.size() / dim;
   mat_quad.SetSize(dim, num_col);
   for (int i = 0; i < num_col; i++)
   {
      for (int j = 0; j < dim; j++)
      {
         mat_quad(j, i) = quad_data[i * dim + j];
      }
   }
}

void GalerkinDifference::GetNeighbourSet(int id, int req_n,
                                         mfem::Array<int> &nels) const
{
   // using mfem mesh object to construct the element patch
   // initialize the patch list
   nels.LoseData();
   nels.Append(id);
   // Creat the adjacent array and fill it with the first layer of adj
   // adjcant element list, candidates neighbors, candidates neighbors' adj
   Array<int> adj, cand, cand_adj, cand_next;
   mesh->ElementToElementTable().GetRow(id, adj);
   cand.Append(adj);
   while (nels.Size() < req_n)
   {
      for (int i = 0; i < adj.Size(); i++)
      {
         if (-1 == nels.Find(adj[i]))
         {
            nels.Append(adj[i]);
         }
      }
      //cout << "List now is: ";
      //nels.Print(cout, nels.Size());
      adj.LoseData();
      for (int i = 0; i < cand.Size(); i++)
      {
         //cout << "deal with cand " << cand[i];
         mesh->ElementToElementTable().GetRow(cand[i], cand_adj);
         //cout << "'s adj are ";
         //cand_adj.Print(cout, cand_adj.Size());
         for (int j = 0; j < cand_adj.Size(); j++)
         {
            if (-1 == nels.Find(cand_adj[j]))
            {
               //cout << cand_adj[j] << " is not found in nels. add to adj and cand_next.\n";
               adj.Append(cand_adj[j]);
               cand_next.Append(cand_adj[j]);
            }
         }
         cand_adj.LoseData();
      }
      cand.LoseData();
      cand = cand_next;
      //cout << "cand copy from next: ";
      //cand.Print(cout, cand.Size());
      cand_next.LoseData();
   }
   cout << "element is " << id << endl;
   cout << "neighbours are " << endl;
   for (int k = 0; k < nels.Size(); ++k)
   {
      cout << nels[k] << endl;
   }
}

void GalerkinDifference::GetElementCenter(int id, mfem::Vector &cent) const
{
   cent.SetSize(mesh->Dimension());
   int geom = mesh->GetElement(id)->GetGeometryType();
   ElementTransformation *eltransf = mesh->GetElementTransformation(id);
   eltransf->Transform(Geometries.GetCenter(geom), cent);
}

void GalerkinDifference::BuildGDProlongation() const
{
   const FiniteElement *fe = fec->FiniteElementForGeometry(Geometry::SEGMENT);
   const int num_dofs = fe->GetDof();
   // allocate the space for the prolongation matrix
   // this step should be done in the constructor (probably)
   // should it be GetTrueVSize() ? or GetVSize()?
   // need a new method that directly construct a CSR format sparsematrix ？
   cP = new mfem::SparseMatrix(GetVSize(), vdim * nEle);
   // determine the minimum # of element in each patch
   int nelmt;
   if (degree % 2 != 0)
   {
      nelmt = degree + 2;
   }
   else
   {
      nelmt = degree + 1;
   }
   cout << "Number of required element: " << nelmt << '\n';
   // loop over all the element:
   // 1. build the patch for each element,
   // 2. construct the local reconstruction operator
   // 3. assemble local reconstruction operator

   // vector that contains element id (resize to zero )
   mfem::Array<int> elmt_id;
   mfem::DenseMatrix cent_mat, quad_mat, local_mat;
   cout << "The size of the prolongation matrix is " << cP->Height() << " x " << cP->Width() << '\n';
   //int degree_actual;
   for (int i = 0; i < nEle; i++)
   {
      GetNeighbourSet(i, nelmt, elmt_id);
      // cout << "neighbours are set " << endl;
      // 2. build the quadrature and barycenter coordinate matrices
      BuildNeighbourMat(elmt_id, cent_mat, quad_mat);
      //cout << "Neighbour mat is built " << endl;
      // 3. buil the loacl reconstruction matrix
      buildLSInterpolation(dim, degree, cent_mat, quad_mat, local_mat);
      // cout << "Local reconstruction matrix built " << endl;
      // cout << "Local reconstruction matrix R:\n";
      // local_mat.Print(cout, local_mat.Width());

      // 4. assemble them back to prolongation matrix
      AssembleProlongationMatrix(elmt_id, local_mat);
      //cout << "assembly done " << endl;
   }
   cP->Finalize();
   cP_is_set = true;
   cout << "Check cP size: " << cP->Height() << " x " << cP->Width() << '\n';
}

void GalerkinDifference::AssembleProlongationMatrix(const mfem::Array<int> &id,
                                                    const DenseMatrix &local_mat) const
{
   // element id coresponds to the column indices
   // dofs id coresponds to the row indices
   // the local reconstruction matrix needs to be assembled `vdim` times
   const FiniteElement *fe = fec->FiniteElementForGeometry(Geometry::SEGMENT);
   const int num_dofs = fe->GetDof();

   int nel = id.Size();

   Array<int> el_dofs;
   Array<int> col_index;
   Array<int> row_index(num_dofs);
   Array<Array<int>> dofs_mat(vdim);

   // Get the id of the element want to assemble in
   int el_id = id[0];

   GetElementVDofs(el_id, el_dofs);
   col_index.SetSize(nel);

   for (int e = 0; e < nel; e++)
   {
      col_index[e] = vdim * id[e];
   }
   for (int v = 0; v < vdim; v++)
   {
      el_dofs.GetSubArray(v * num_dofs, num_dofs, row_index);
      // cout << "local mat will be assembled into: ";
      // row_index.Print(cout, num_dofs);
      // cout << endl;
      cP->SetSubMatrix(row_index, col_index, local_mat, 1);
      row_index.LoseData();
      // elements id also need to be shift accordingly
      col_index.SetSize(nel);
      for (int e = 0; e < nel; e++)
      {
         col_index[e]++;
      }
   }
}

void buildLSInterpolation(int dim, int degree, const DenseMatrix &x_center,
                          const DenseMatrix &x_quad, DenseMatrix &interp)
{
   // get the number of quadrature points and elements.
   int num_quad = x_quad.Width();
   int num_elem = x_center.Width();

   // number of total polynomial basis functions
   int num_basis = -1;
   num_basis = degree + 1;
   // Construct the generalized Vandermonde matrix
   mfem::DenseMatrix V(num_elem, num_basis);
   for (int i = 0; i < num_elem; ++i)
   {
      double dx = x_center(0, i) - x_center(0, 0);
      for (int p = 0; p <= degree; ++p)
      {
         V(i, p) = pow(dx, p);
      }
   }
   // Set the RHS for the LS problem (it's the identity matrix)
   // This will store the solution, that is, the basis coefficients, hence
   // the name `coeff`
   mfem::DenseMatrix coeff(num_elem, num_elem);
   coeff = 0.0;
   for (int i = 0; i < num_elem; ++i)
   {
      coeff(i, i) = 1.0;
   }
   // Set-up and solve the least-squares problem using LAPACK's dgels
   char TRANS = 'N';
   int info;
   int lwork = 2 * num_elem * num_basis;
   double work[lwork];
   dgels_(&TRANS, &num_elem, &num_basis, &num_elem, V.GetData(), &num_elem,
          coeff.GetData(), &num_elem, work, &lwork, &info);
   MFEM_ASSERT(info == 0, "Fail to solve the underdetermined system.\n");
   // Perform matrix-matrix multiplication between basis functions evalauted at
   // quadrature nodes and basis function coefficients.
   interp.SetSize(num_quad, num_elem);
   interp = 0.0;
   // loop over quadrature points
   for (int j = 0; j < num_quad; ++j)
   {
      double dx = x_quad(0, j) - x_center(0, 0);
      // loop over the element centers
      for (int i = 0; i < num_elem; ++i)
      {
         for (int p = 0; p <= degree; ++p)
         {
            interp(j, i) += pow(dx, p) * coeff(p, i);
         }
      }
   }
}

///functions related to CentGridFunction class
CentGridFunction::CentGridFunction(FiniteElementSpace *f)
{
   SetSize(f->GetVDim() * f->GetNE());
   fes = f;
   fec = NULL;
   sequence = f->GetSequence();
   UseDevice(true);
}

void CentGridFunction::ProjectCoefficient(VectorCoefficient &coeff)
{
   int vdim = fes->GetVDim();
   Array<int> vdofs(vdim);
   Vector vals;

   int geom = fes->GetMesh()->GetElement(0)->GetGeometryType();
   const IntegrationPoint &cent = Geometries.GetCenter(geom);
   const FiniteElement *fe;
   ElementTransformation *eltransf;
   for (int i = 0; i < fes->GetNE(); i++)
   {
      fe = fes->GetFE(i);
      // Get the indices of dofs
      for (int j = 0; j < vdim; j++)
      {
         vdofs[j] = i * vdim + j;
      }

      eltransf = fes->GetElementTransformation(i);
      eltransf->SetIntPoint(&cent);
      vals.SetSize(vdofs.Size());
      coeff.Eval(vals, *eltransf, cent);

      if (fe->GetMapType() == 1)
      {
         vals(i) *= eltransf->Weight();
      }
      SetSubVector(vdofs, vals);
   }
}

CentGridFunction &CentGridFunction::operator=(const Vector &v)
{
   std::cout << "cent = is called.\n";
   MFEM_ASSERT(fes && v.Size() == fes->GetTrueVSize(), "");
   Vector::operator=(v);
   return *this;
}

CentGridFunction &CentGridFunction::operator=(double value)
{
   Vector::operator=(value);
   return *this;
}