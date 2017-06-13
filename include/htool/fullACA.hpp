#ifndef ACA_HPP
#define ACA_HPP

#include <iostream>
#include <fstream>
#include <complex>
#include <vector>
#include <cassert>
#include "lrmat.hpp"


namespace htool {
//================================//
//   CLASSE MATRICE RANG FAIBLE   //
//================================//
//
// Refs biblio:
//
//  -> slides de Stéphanie Chaillat:
//           http://uma.ensta-paristech.fr/var/files/chaillat/seance2.pdf
//           et en particulier la slide 25
//
//  -> livre de M.Bebendorf:
//           http://www.springer.com/kr/book/9783540771463
//           et en particulier le paragraphe 3.4
//
//  -> livre de Rjasanow-Steinbach:
//           http://www.ems-ph.org/books/book.php?proj_nr=125
//           et en particulier le paragraphe 3.2
//
//=================================//
template<typename T>
class fullACA: public LowRankMatrix<T>{


private:
	// No assignement or copy
	// fullACA(const fullACA& copy_from);
	// fullACA & operator=(const fullACA& copy_from);

public:
	//=========================//
	//    FULL PIVOT ACA    //
	//=========================//
    // If reqrank=-1 (default value), we use the precision given by epsilon for the stopping criterion;
    // otherwise, we use the required rank for the stopping criterion (!: at the end the rank could be lower)
	fullACA(const std::vector<int>& ir0, const std::vector<int>& ic0, const Cluster& t0,const Cluster& s0, int rank0=-1):LowRankMatrix<T>(ir0,ic0,t0,s0,rank0){}

	// fullACA(fullACA&&) = default; // move constructor
  // fullACA& operator=(fullACA&&) = default; // move assignement operator

	void build(const IMatrix<T>& A){
		if(this->rank == 0){
			this->U.resize(this->nr,1);
			this->V.resize(1,this->nc);
		}
		else{

			// Matrix assembling
			Matrix<T> M(this->ir.size(),this->ic.size());

			for (int i=0; i<M.nb_rows(); i++){
				for (int j=0; j<M.nb_cols(); j++){
					M(i,j) = A.get_coef(this->ir[i], this->ic[j]);
				}
			}

			// Full pivot
			int q=1;
			int reqrank = this->rank;
			std::vector<std::vector<T> > uu;
			std::vector<std::vector<T> > vv;
			double Norm = normFrob(M);

			while (((reqrank > 0) && (q < reqrank) ) ||
			      ( (reqrank < 0) && ( normFrob(M)>this->epsilon ) )) {
				if (q*(this->nr+this->nc) > (this->nr*this->nc)) { // the current rank would not be advantageous
					std::cout << "Pas avantageux" << std::endl;
					break;
				}
				else{
					std::pair<int , int > ind = argmax(M);
					T pivot = M(ind.first,ind.second);
					if (std::abs(pivot)<1e-15) break;
					uu.push_back(M.get_col(ind.second));
					vv.push_back(M.get_row(ind.first)/pivot);

					for (int i =0 ; i<M.nb_rows();i++){
						for (int j =0 ; j<M.nb_cols();j++){
							M(i,j)-=uu[q-1][i]*vv[q-1][j];
						}
					}
					q+=1;
				}
			}
			this->rank=q-1;
			if (this->rank==0){
				this->U.resize(this->nr,1);
				this->V.resize(1,this->nc);
			}
			else{
				this->U.resize(this->nr,this->rank);
				this->V.resize(this->rank,this->nc);
				for (int k=0;k<this->rank;k++){
					this->U.set_col(k,uu[k]);
					this->V.set_row(k,vv[k]);
				}
			}
		}
	}

};

}
#endif
