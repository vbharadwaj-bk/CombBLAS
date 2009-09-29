/****************************************************************/
/* Sequential and Parallel Sparse Matrix Multiplication Library */
/* version 2.3 --------------------------------------------------/
/* date: 01/18/2009 ---------------------------------------------/
/* author: Aydin Buluc (aydin@cs.ucsb.edu) ----------------------/
/****************************************************************/

#include <mpi.h>
#include <sys/time.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>  // Required for stringstreams
#include <ctime>
#include <cmath>
#include "../SpParVec.h"
#include "../SpTuples.h"
#include "../SpDCCols.h"
#include "../SpParMat.h"
#include "../DenseParMat.h"
#include "../DenseParVec.h"

using namespace std;


// Simple helper class for declarations: Just the numerical type is templated 
// The index type and the sequential matrix type stays the same for the whole code
// In this case, they are "int" and "SpDCCols"
template <class NT>
class PSpMat 
{ 
public: 
	typedef SpDCCols < int, NT > DCCols;
	typedef SpParMat < int, NT, DCCols > MPI_DCCols;
};


int main(int argc, char* argv[])
{
	MPI::Init(argc, argv);
	int nprocs = MPI::COMM_WORLD.Get_size();
	int myrank = MPI::COMM_WORLD.Get_rank();
	
	typedef PlusTimesSRing<bool, int> PTBOOLINT;	
	typedef PlusTimesSRing<bool, double> PTBOOLDOUBLE;

	if(argc < 4)
        {
		if(myrank == 0)
		{	
                	cout << "Usage: ./betwcent <BASEADDRESS> <K4APPROX> <BATCHSIZE>" << endl;
                	cout << "Example: ./betwcent Data/ 15 128" << endl;
                	cout << "Input file input.txt should be under <BASEADDRESS> in triples format" << endl;
                	cout << "<BATCHSIZE> should be a multiple of sqrt(p)" << endl;
			cout << "Because <BATCHSIZE> is for the overall matrix, whereas <K4APPROX> is per processor " << endl;
 		}
		MPI::Finalize(); 
		return -1;
        }

	{
		int K4Approx = atoi(argv[2]);
		int batchSize = atoi(argv[3]);

		string directory(argv[1]);		
		string ifilename = "input.txt";
		ifilename = directory+"/"+ifilename;

		ifstream input(ifilename.c_str());
		MPI::COMM_WORLD.Barrier();
	
		PSpMat<bool>::MPI_DCCols A, AT;	// construct object
		AT.ReadDistribute(input, 0);	// read it from file, note that we use the transpose of "input" data
		A = AT;
		A.Transpose();

		input.clear();
		input.close();
			
		int nPasses = (int) pow(2.0, K4Approx);
		int numBatches = (int) ceil( static_cast<float>(nPasses)/ static_cast<float>(batchSize));
	
		// get the number of batch vertices for submatrix
		int subBatchSize = batchSize / (AT.getcommgrid())->GetGridCols();
		if(batchSize % (AT.getcommgrid())->GetGridCols() > 0 && myrank == 0)
			cout << "*** Please make batchsize divisible by the grid dimensions (r and s) ***" << endl;

		vector<int> candidates;
		if (myrank == 0)
			cout << "Batch processing will occur " << numBatches << " times, each processing " << batchSize << " vertices (overall)" << endl;

		// Only consider non-isolated vertices
		int vertices = 0;
		int vrtxid = 0; 
		int nlocpass = nPasses / (AT.getcommgrid())->GetGridCols();
		while(vertices < nlocpass)
		{
			vector<int> single;
			vector<int> empty;
			single.push_back(vrtxid);		// will return ERROR if vrtxid > N (the column dimension) 
			int locnnz = ((AT.seq())(empty,single)).getnnz();
			int totnnz;
			(AT.getcommgrid())->GetColWorld().Allreduce( &locnnz, &totnnz, 1, MPI::INT, MPI::SUM);
					
			if(totnnz > 0)
			{
				candidates.push_back(vrtxid);
				++vertices;
			}
			++vrtxid;
		}

		double t1 = MPI_Wtime();
		vector<int> batch(subBatchSize);
		DenseParVec<int, double> bc(AT.getcommgrid(),0.0);

		for(int i=0; i< numBatches; ++i)
		{
			for(int j=0; j< subBatchSize; ++j)
			{
				batch[j] = candidates[i*subBatchSize + j];
			}
			
			PSpMat<int>::MPI_DCCols fringe = AT.SubsRefCol(batch);
			fringe.PrintInfo();

			// Create nsp by setting (r,i)=1 for the ith root vertex with label r
			// Inially only the diagonal processors have any nonzeros (because we chose roots so)
			PSpMat<int>::DCCols * nsploc = new PSpMat<int>::DCCols();
			tuple<int, int, int> * mytuples = NULL;	
			if(AT.getcommgrid()->GetRankInProcRow() == AT.getcommgrid()->GetRankInProcCol())
			{
				mytuples = new tuple<int, int, int>[subBatchSize];
				for(int k =0; k<subBatchSize; ++k)
				{
					mytuples[k] = make_tuple(batch[k], k, 1);
				}
				nsploc->Create( subBatchSize, AT.getlocalrows(), subBatchSize, mytuples);		
			}
			else
			{  
				nsploc->Create( 0, AT.getlocalrows(), subBatchSize, mytuples);		
			}
		
			PSpMat<int>::MPI_DCCols  nsp(nsploc, AT.getcommgrid());			
				
			vector < PSpMat<bool>::MPI_DCCols * > bfs;		// internally keeps track of depth

			while( fringe.getnnz() > 0 )
			{
				nsp += fringe;
				PSpMat<bool>::MPI_DCCols * level = new PSpMat<bool>::MPI_DCCols( fringe ); 
				bfs.push_back(level);

				fringe = Mult_AnXBn_ActiveTarget<PTBOOLINT>(AT, fringe);
				fringe.PrintInfo();
				fringe = EWiseMult(fringe, nsp, true);
			}
			MPI::COMM_WORLD.Barrier();
			cout << "Exit forward" << endl;

			// Apply the unary function 1/x to every element in the matrix
			// 1/x works because no explicit zeros are stored in the sparse matrix nsp
			PSpMat<double>::MPI_DCCols nspInv = nsp;
			nspInv.Apply(bind1st(divides<double>(), 1));

			// create a dense matrix with all 1's 
			DenseParMat<int, double> bcu(1.0, AT.getcommgrid(), fringe.getlocalrows(), fringe.getlocalcols() );

			// BC update for all vertices except the sources
			for(int j = bfs.size()-1; j > 0; --j)
			{
				PSpMat<double>::MPI_DCCols w = EWiseMult( *bfs[j], nspInv, false);
				w.EWiseScale(bcu);

				PSpMat<double>::MPI_DCCols product = Mult_AnXBn_ActiveTarget<PTBOOLDOUBLE>(A,w);
				product = EWiseMult(product, *bfs[j-1], false);
				product = EWiseMult(product, nsp, false);		

				bcu += product;
			}
			for(int j=0; j < bfs.size(); ++j)
			{
				delete bfs[j];
			}
		
			cout << "Adding contributions to bc" << endl;
			bc += bcu.Reduce(Column, plus<double>(), 0.0);
		}
		bc.Apply(bind2nd(minus<double>(), nPasses));	// Subtrack nPasses from all the bc scores (because bcu was initialized to all 1's)
		
		double t2=MPI_Wtime();
		double TEPS = (nPasses * static_cast<float>(A.getnnz())) / (t2-t1);
		if( myrank == 0)
		{
			cout<<"Computation finished"<<endl;	
			fprintf(stdout, "%.6lf seconds elapsed for %d starting vertices\n", t2-t1, nPasses);
			fprintf(stdout, "TEPS score is: %.6lf\n", TEPS);
		}
	}	

	// make sure the destructors for all objects are called before MPI::Finalize()
	MPI::Finalize();	
	return 0;
}

