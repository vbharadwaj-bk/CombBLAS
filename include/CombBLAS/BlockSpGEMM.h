#ifndef _BLOCK_SPGEMM_H_
#define _BLOCK_SPGEMM_H_

#include "CombBLAS.h"


namespace combblas
{


template <typename IT,
		  typename NTA,
		  typename DERA,
		  typename NTB,
		  typename DERB>
struct BlockSpGEMM
{

private:

	std::vector<std::vector<SpParMat<IT, NTA, DERA>>> A_blocks_;
	std::vector<std::vector<SpParMat<IT, NTB, DERB>>> B_blocks_;
	int br_, bc_, bi_, cur_block_;
	IT nr_, nc_;



	
public:

	BlockSpGEMM (SpParMat<IT, NTA, DERA>	&A,
				 SpParMat<IT, NTB, DERB>	&B,
				 int						 br,
				 int						 bc,
				 int						 bi = 1
				 ) :
		br_(br), bc_(bc), bi_(bi), cur_block_(0)
	{
		A_blocks_ = A.BlockSplit(br_, bi_);
		B_blocks_ = B.BlockSplit(bi_, bc_);
		nr_		  = A.getnrow();
		nc_		  = B.getncol();
	}



	template<typename SR,
			 typename NTC,
			 typename DERC>
	SpParMat<IT, NTC, DERC>
	getNextBlock (IT &roffset, IT &coffset)
	{
		assert(bi_ == 1);
		
		int rbid = cur_block_ / bc_;
		int cbid = cur_block_ % bc_;
		++cur_block_;

		IT	bs = nr_ / br_;
		IT	r  = nr_ % br_;
		roffset = (std::min(static_cast<IT>(rbid), r)*(bs+1)) +
			(std::max(static_cast<IT>(0), rbid-r)*bs);

		bs = nc_ / bc_;
		r  = nc_ % bc_;
		coffset = (std::min(static_cast<IT>(cbid), r)*(bs+1)) +
			(std::max(static_cast<IT>(0), cbid-r)*bs);

		return Mult_AnXBn_DoubleBuff<SR, NTC, DERC>
			(A_blocks_[rbid][0], B_blocks_[0][cbid], false, false);
	}



	bool
	hasNext ()
	{
		return cur_block_ < br_*bc_;
	}



	template<typename SR,
			 typename NTC,
			 typename DERC>
	SpParMat<IT, NTC, DERC>
	getBlockId (int rbid, int cbid, IT &roffset, IT &coffset)
	{
		assert(bi_ == 1);
		
		IT	bs = nr_ / br_;
		IT	r  = nr_ % br_;
		roffset = (std::min(static_cast<IT>(rbid), r)*(bs+1)) +
			(std::max(static_cast<IT>(0), rbid-r)*bs);

		bs = nc_ / bc_;
		r  = nc_ % bc_;
		coffset = (std::min(static_cast<IT>(cbid), r)*(bs+1)) +
			(std::max(static_cast<IT>(0), cbid-r)*bs);

		return Mult_AnXBn_DoubleBuff<SR, NTC, DERC>
			(A_blocks_[rbid][0], B_blocks_[0][cbid], false, false);
	}
};

	
}


#endif
