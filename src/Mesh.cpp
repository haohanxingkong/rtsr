#include "Mesh.hpp"

#include "igl/embree/line_mesh_intersection.h"
#include "igl/fit_plane.h"
#include "igl/mat_min.h"
#include "igl/barycentric_to_global.h"

#include <Eigen/QR>

#include <iostream>
#include <cstdlib>

template <typename A, typename B>
Eigen::Matrix<typename B::Scalar, A::RowsAtCompileTime, A::ColsAtCompileTime> 
  extract(const Eigen::DenseBase<B>& full, const Eigen::DenseBase<A>& ind)
{
  using target_t = Eigen::Matrix <typename B::Scalar, A::RowsAtCompileTime, A::ColsAtCompileTime>;
  int num_indices = ind.innerSize();
  target_t target(num_indices);
  
#pragma omp parallel for
  for (int i = 0; i < num_indices; ++i)
    target[i] = full[ind[i]];
  
  return target;
} 

template <typename Derived>
void remove_empty_rows(const Eigen::MatrixBase<Derived>& in, Eigen::MatrixBase<Derived> const& out_)
{
  Eigen::Matrix<bool, Eigen::Dynamic, 1> non_minus_one(in.rows(), 1);
  
#pragma omp parallel for
  for (int i = 0; i < in.rows(); ++i)
  {
    if (in.row(i).isApprox(Eigen::Matrix<typename Derived::RealScalar, 1, 3>(-1., 0., 0.)))
      non_minus_one(i) = false;
    else
      non_minus_one(i) = true;
  }
  
  typename Eigen::MatrixBase<Derived>& out = const_cast<Eigen::MatrixBase<Derived>&>(out_);
  out.derived().resize(non_minus_one.count(), in.cols());

  typename Eigen::MatrixBase<Derived>::Index j = 0;
  for(typename Eigen::MatrixBase<Derived>::Index i = 0; i < in.rows(); ++i)
  {
    if (non_minus_one(i))
      out.row(j++) = in.row(i);
  }
}

template <typename InType>
Eigen::Matrix<InType, 3, 1> get_basis(const Eigen::Matrix<InType, 3, 1>& n) {

  Eigen::Matrix<InType, 3, 3> R;

  Eigen::Matrix<InType, 3, 1> Q = n;
  const Eigen::Matrix<InType, 3, 1> absq = Q.cwiseAbs();

  Eigen::Matrix<int,1,1> min_idx;
  Eigen::Matrix<InType,1,1> min_elem;
  
  igl::mat_min(absq, 1, min_elem, min_idx);
  
  Q(min_idx(0)) = 1;

  Eigen::Matrix<InType, 3, 1> T = Q.cross(n).normalized();
  Eigen::Matrix<InType, 3, 1> B = n.cross(T).normalized();

  R.col(0) = T;
  R.col(1) = B;
  R.col(2) = n;

  return R;
}

template <typename T>
Mesh<T>::Mesh()
{
  
}

template <typename T>
Mesh<T>::~Mesh()
{
  
}

template <typename T>
template <int Rows, int Cols>
void Mesh<T>::align_to_point_cloud(const Eigen::Matrix<T, Rows, Cols>& P)
{  
  using TvecR3 = Eigen::Matrix<T, 1, 3>;
  using TvecC3 = Eigen::Matrix<T, 3, 1>;
  
  const TvecR3 bb_min = P.colwise().minCoeff();
  const TvecR3 bb_max = P.colwise().maxCoeff();
  const TvecR3 bb_d = (bb_max - bb_min).cwiseAbs();
  
  const Eigen::Transform<T, 3, Eigen::Affine> scaling(Eigen::Scaling(TvecC3(MESH_SCALING_FACTOR*bb_d(0)/(MESH_RESOLUTION-1), 0.,MESH_SCALING_FACTOR*bb_d(2)/(MESH_RESOLUTION-1))));
  
  const TvecR3 pc_mean = P.colwise().mean();
  TvecR3 P_centr = bb_min + 0.5*(bb_max - bb_min);
  P_centr(1) = pc_mean(1); // Move to mean height w/r to pc instead of bb.
  
  const Eigen::Transform<T, 3, Eigen::Affine> t(Eigen::Translation<T, 3>(P_centr - Eigen::Matrix<T, 1, 3>(0,0,0))); // Remove the zero vector if you dare ;)
  
  transform = t.matrix();
    
  V.resize(MESH_RESOLUTION*MESH_RESOLUTION, 3);
  F.resize((MESH_RESOLUTION-1)*(MESH_RESOLUTION-1)*2, 3);
  JtJ.resize(MESH_RESOLUTION);
  Jtz.resize(MESH_RESOLUTION);
  h = Eigen::Matrix<T, Eigen::Dynamic, 1>::Zero(MESH_RESOLUTION*MESH_RESOLUTION);

#pragma omp parallel for
  for (int z_step = 0; z_step < MESH_RESOLUTION; ++z_step)
  {
    for (int x_step = 0; x_step < MESH_RESOLUTION; ++x_step)
    {
      Eigen::Matrix<T, 1, 4> v; v << TvecR3(x_step-(MESH_RESOLUTION-1)/2.f,1.f,z_step-(MESH_RESOLUTION-1)/2.f),1.f;
      V.row(x_step + z_step*MESH_RESOLUTION) << (v * scaling.matrix().transpose() * transform.transpose()).template head<3>();
    }
  }
  
#pragma omp parallel for
  for (int y_step = 0; y_step < MESH_RESOLUTION-1; ++y_step)
  {
    for (int x_step = 0; x_step < MESH_RESOLUTION-1; ++x_step)
    {
      // JtJ matrix implementation depends on this indexing, if you hange this you need to change the JtJ class.
      F.row(x_step*2 + y_step*(MESH_RESOLUTION-1)*2)     << x_step+   y_step   *MESH_RESOLUTION,x_step+1+y_step*   MESH_RESOLUTION,x_step+(y_step+1)*MESH_RESOLUTION;
      F.row(x_step*2 + y_step*(MESH_RESOLUTION-1)*2 + 1) << x_step+1+(y_step+1)*MESH_RESOLUTION,x_step+ (y_step+1)*MESH_RESOLUTION,x_step+1+y_step*MESH_RESOLUTION;
    }
  }
      
  for (int i = 0; i < (MESH_RESOLUTION-1)*(MESH_RESOLUTION-1)*2; ++i)
  {
    JtJ.update_triangle(i, 0.34f, 0.33f);
  }

  for (int i = 0; i < (MESH_RESOLUTION-1)*(MESH_RESOLUTION-1)*2; ++i)
  {
    Jtz.update_triangle(i, 0.34f, 0.33f, pc_mean(1));
  }
}

template <typename T>
const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>& Mesh<T>::vertices()
{
  return this->V;
}

template <typename T>
const Eigen::MatrixXi& Mesh<T>::faces()
{
  return this->F;
}

template <typename T>
template <int Rows, int Cols>
void Mesh<T>::set_target_point_cloud(const Eigen::Matrix<T, Rows, Cols>& P)
{
  // Seems to be a bug in embree. tnear of a ray is not set corretly if vector is
  // along coordinate axis, needs slight offset.
  Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> normals = Eigen::Matrix<T, 1, 3>(0.0001, 1., 0.0).replicate(P.rows(), 1);
  Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic> bc = igl::embree::line_mesh_intersection(P, normals, V, F);
          
  for (int i = 0; i < bc.rows(); ++i)
  {
    const Eigen::Matrix<T, 1, 3>& row = bc.row(i);

    if (static_cast<int>(row(0)) == -1)
      continue;
    
    JtJ.update_triangle(static_cast<int>(row(0)), row(1), row(2));
    
    Jtz.update_triangle(static_cast<int>(row(0)), row(1), row(2), P.row(i)(1));
  }
}

template <typename T>
void Mesh<T>::iterate()
{
  gauss_seidel(h, 1);

  V.col(1) = h;
}

template <typename T>
template <int HRows>
void Mesh<T>::gauss_seidel(Eigen::Matrix<T, HRows, 1>& h, int iterations) const
{
  const auto& Jtz_vec = Jtz.get_vec();
    
  for(int it = 0; it < iterations; it++)
  {
    for (int i = 0; i < h.rows(); i++)
    {
      T xn = Jtz_vec(i);
      T acc = 0;
      
      std::array<double, 6> vals;
      std::array<int, 6> ids;
      
      double a;
      JtJ.get_matrix_values_for_vertex(i, vals, ids, a);
            
      for (int j = 0; j < 6; ++j)
      {
        if (ids[j] == -1)
          continue;
          
        acc += vals[j] * h(ids[j]);
      }
      
      xn -= acc;
      
      h(i) = xn/a;
    }
  }
}

// Explicit instantiation
template class Mesh<double>;
template void Mesh<double>::align_to_point_cloud(const Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic>&);
template void Mesh<double>::set_target_point_cloud(const Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic>&);
template void Mesh<double>::gauss_seidel(Eigen::Matrix<double, Eigen::Dynamic, 1>&, int iterations) const;

//template class Mesh<float>;
//template void Mesh<float>::align_to_point_cloud(const Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic>&);
//template void Mesh<float>::solve(const Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic>&);
