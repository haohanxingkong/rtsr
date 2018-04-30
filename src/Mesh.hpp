#ifndef MESH_HPP
#define MESH_HPP

#include <Eigen/StdVector>
#include <Eigen/Geometry>
#include <cassert>
#include <ostream>

#include "EqHelpers.hpp"

// Number of vertices along one dimension
#define MESH_RESOLUTION 30
// Scale factor. 1 makes the mesh the same size as the bb of the
// pc given to align_to_point_cloud
#define MESH_SCALING_FACTOR 1.4
#define MESH_LEVELS 2

template <typename T>
class Mesh
{
public:
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
  Mesh();
  ~Mesh();
  
  template <int Rows, int Cols>
  void align_to_point_cloud(const Eigen::Matrix<T, Rows, Cols>& P);// Basically resets the mesh
  
  template <int Rows, int Cols>
  void set_target_point_cloud(const Eigen::Matrix<T, Rows, Cols>& P);
  void iterate();
  
  const Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>& vertices(const unsigned int level);
  const Eigen::MatrixXi& faces(const unsigned int level);
  
private:
  std::vector<JtJMatrixGrid<T>, Eigen::aligned_allocator<JtJMatrixGrid<T>>> JtJ;
  std::vector<JtzVector<T>, Eigen::aligned_allocator<JtzVector<T>>> Jtz;
  
  std::vector<Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>, Eigen::aligned_allocator<Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>>> V; // Vertices
  std::vector<Eigen::MatrixXi, Eigen::aligned_allocator<Eigen::MatrixXi>> F; // Face vertex indices
  Eigen::Matrix<T, 4, 4> transform; // Mesh location and orientation

  template <int Iterations>
  void sor(Eigen::Ref<Eigen::Matrix<T, Eigen::Dynamic, 1>> h) const;
  
  template <int Iterations>
  void sor_parallel(Eigen::Ref<Eigen::Matrix<T, Eigen::Dynamic, 1>> h) const;
};

#endif // MESH_HPP
