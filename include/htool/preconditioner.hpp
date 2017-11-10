#ifndef HTOOL_PRECONDITIONER_HPP
#define HTOOL_PRECONDITIONER_HPP

#include "matrix.hpp"
#include "lapack.hpp"
#include "wrapper_mpi.hpp"
#include "wrapper_hpddm.hpp"

namespace htool{

template<template<typename> class LowRankMatrix, typename T>
class DDM{
private:

  int n;
  int n_inside;
  const std::vector<int> neighbors;
  // const std::vector<int> cluster_to_ovr_subdomain;
  std::vector<std::vector<int> > intersections;
  std::vector<T> vec_ovr;
  std::vector<std::vector<T>> snd,rcv;
  HPDDMDense<LowRankMatrix,T> hpddm_op;
  std::vector<T> mat_loc;
  std::vector<double> D;
  MPI_Comm comm;


public:
  DDM(const IMatrix<T>& mat0, const HMatrix<LowRankMatrix,T>& hmat_0,
    const std::vector<int>&  ovr_subdomain_to_global0,
    const std::vector<int>& cluster_to_ovr_subdomain0,
    const std::vector<int>& neighbors0,
    const std::vector<std::vector<int> >& intersections0,
    MPI_Comm comm0=MPI_COMM_WORLD): hpddm_op(hmat_0), n(ovr_subdomain_to_global0.size()), n_inside(cluster_to_ovr_subdomain0.size()), neighbors(neighbors0), vec_ovr(n),mat_loc(n*n), D(n), comm(comm0) {

    std::vector<int> renum(n,-1);
    std::vector<int> renum_to_global(n);

    for (int i=0;i<cluster_to_ovr_subdomain0.size();i++){
      renum[cluster_to_ovr_subdomain0[i]]=i;
      renum_to_global[i]=ovr_subdomain_to_global0[cluster_to_ovr_subdomain0[i]];
    }
    int count =cluster_to_ovr_subdomain0.size();
    // std::cout << count << std::endl;
    for (int i=0;i<n;i++){
      if (renum[i]==-1){
        renum[i]=count;
        renum_to_global[count++]=ovr_subdomain_to_global0[i];
      }
    }


    intersections.resize(neighbors.size());
    for (int i=0;i<neighbors.size();i++){
      intersections[i].resize(intersections0[i].size());
      for (int j=0;j<intersections[i].size();j++){
        intersections[i][j]=renum[intersections0[i][j]];
      }
    }

    // Building Ai
    bool sym=false;
    const std::vector<int>& MyDiagFarFieldMats = hpddm_op.HA.get_MyDiagFarFieldMats();
    const std::vector<int>& MyDiagNearFieldMats= hpddm_op.HA.get_MyDiagNearFieldMats();

    // Internal dense blocks
    const std::vector<SubMatrix<T>>& MyNearFieldMats = hpddm_op.HA.get_MyNearFieldMats();
    for (int i=0;i<MyDiagNearFieldMats.size();i++){
      const SubMatrix<T>& submat = MyNearFieldMats[MyDiagNearFieldMats[i]];
      int local_nr = submat.nb_rows();
      int local_nc = submat.nb_cols();
      int offset_i = submat.get_offset_i()-hpddm_op.HA.get_local_offset();;
      int offset_j = submat.get_offset_j()-hpddm_op.HA.get_local_offset();
      for (int i=0;i<local_nc;i++){
        std::copy_n(&(submat(0,i)),local_nr,&mat_loc[offset_i+(offset_j+i)*n]);
      }
    }

    // Internal compressed block
    const std::vector<LowRankMatrix<T>>& MyFarFieldMats = hpddm_op.HA.get_MyFarFieldMats();
    Matrix<T> FarFielBlock(n,n);
    for (int i=0;i<MyDiagFarFieldMats.size();i++){
      const LowRankMatrix<T>& lmat = MyFarFieldMats[MyDiagFarFieldMats[i]];
      int local_nr = lmat.nb_rows();
      int local_nc = lmat.nb_cols();
      int offset_i = lmat.get_offset_i()-hpddm_op.HA.get_local_offset();
      int offset_j = lmat.get_offset_j()-hpddm_op.HA.get_local_offset();;
      FarFielBlock.resize(local_nr,local_nc);
      lmat.get_whole_matrix(&(FarFielBlock(0,0)));
      for (int i=0;i<local_nc;i++){
        std::copy_n(&(FarFielBlock(0,i)),local_nr,&mat_loc[offset_i+(offset_j+i)*n]);
      }
    }


    // Overlap
    std::vector<T> horizontal_block(n-n_inside,n_inside),vertical_block(n,n-n_inside);
    horizontal_block = mat0.get_submatrix(std::vector<int>(renum_to_global.begin()+n_inside,renum_to_global.end()),std::vector<int>(renum_to_global.begin(),renum_to_global.begin()+n_inside)).get_mat();
    vertical_block = mat0.get_submatrix(renum_to_global,std::vector<int>(renum_to_global.begin()+n_inside,renum_to_global.end())).get_mat();
    for (int j=0;j<n_inside;j++){
      std::copy_n(horizontal_block.begin()+j*(n-n_inside),n-n_inside,&mat_loc[n_inside+j*n]);
    }
    for (int j=n_inside;j<n;j++){
      std::copy_n(vertical_block.begin()+(j-n_inside)*n,n,&mat_loc[j*n]);
    }

    // mat_loc= mat0.get_submatrix(renum_to_global,renum_to_global).get_mat();

    hpddm_op.initialize(n, sym, mat_loc.data(), neighbors, intersections);

    fill(D.begin(),D.begin()+n_inside,1);
    fill(D.begin()+n_inside,D.end(),0);

    hpddm_op.HPDDMDense<LowRankMatrix,T>::super::super::initialize(D.data());
    hpddm_op.callNumfact();


  }

  void solve(const T* const rhs, T* const x){
    //
    int rankWorld = hpddm_op.HA.get_rankworld();
    int sizeWorld = hpddm_op.HA.get_sizeworld();
    int offset = hpddm_op.HA.get_local_offset();
    int size   = hpddm_op.HA.get_local_size();

    //
    std::vector<T> rhs_perm(hpddm_op.HA.nb_cols());
    std::vector<T> x_local(n,0);

    // Permutation
    hpddm_op.HA.source_to_cluster_permutation(rhs,rhs_perm.data());

    // Local rhs
    std::vector<T> local_rhs(n,0);
    std::copy_n(rhs_perm.begin()+offset,n_inside,local_rhs.begin());
    // TODO avoid com here
    // for (int i=0;i<n-n_inside;i++){
    //   local_rhs[i]=rhs_perm[]
    // }
    hpddm_op.exchange(local_rhs.data(), 1);

    // Solve
    HPDDM::IterativeMethod::solve(hpddm_op, local_rhs.data(), x_local.data(), 1,comm);

    // Local to global
    hpddm_op.HA.local_to_global(x_local.data(),hpddm_op.in_global->data());

    // Permutation
    hpddm_op.HA.cluster_to_target_permutation(hpddm_op.in_global->data(),x);

  }


};

}
#endif
