#ifndef DRL4AMR_HPP
#define DRL4AMR_HPP

#include "mfem.hpp"

using namespace std;
using namespace mfem;

class Drl4Amr
{
private:
   const int max_dofs = 5000;
   const int max_amr_depth = 4;
   const int nx = 4;
   const int ny = 4;
   const Element::Type type = Element::QUADRILATERAL;
   const bool generate_edges = true;
   const double sx = 1.0;
   const double sy = 1.0;
   const bool sfc = false; // space-filling curve ordering
   const int order = 1;
   const char *device_config = "cpu";
   const bool visualization = true;
   const char *vishost = "localhost";
   const int visport = 19916;
   socketstream vis[3];

   Device device;
   Mesh mesh;
   const int dim;
   const int sdim;
   H1_FECollection fec;
   FiniteElementSpace fespace;
   ConstantCoefficient one;
   ConstantCoefficient zero;
   BilinearFormIntegrator *integ;
   FunctionCoefficient xcoeff;
   GridFunction x;
   int iteration;
   FiniteElementSpace flux_fespace;
   ZienkiewiczZhuEstimator estimator;
   ThresholdRefiner refiner;
public:
   Drl4Amr(int i);
   int Compute();
   int Refine(int el =-1);
   int GetNDofs() { return fespace.GetNDofs(); }
   int GetNE() { return fespace.GetNE(); }
   double GetNorm();
   void GetImage();
};

#endif // DRL4AMR_HPP