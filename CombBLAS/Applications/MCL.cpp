/****************************************************************/
/* Parallel Combinatorial BLAS Library (for Graph Computations) */
/* version 1.6 -------------------------------------------------*/
/* date: 05/15/2016 --------------------------------------------*/
/* authors: Ariful Azad, Aydin Buluc, Adam Lugowski ------------*/
/****************************************************************/
/*
 Copyright (c) 2010-2016, The Regents of the University of California
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */

#include <mpi.h>

// These macros should be defined before stdint.h is included
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif
#include <stdint.h>

#include <sys/time.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>  // Required for stringstreams
#include <ctime>
#include <cmath>
#include "../CombBLAS.h"

using namespace std;

#define EPS 0.001


class Dist
{ 
public: 
	typedef SpDCCols < int64_t, float > DCCols;
	typedef SpParMat < int64_t, float, DCCols > MPI_DCCols;
	typedef FullyDistVec < int64_t, float> MPI_DenseVec;
};


void Interpret(const Dist::MPI_DCCols & A)
{
	// Placeholder
}


float Inflate(Dist::MPI_DCCols & A, float power)
{
	A.Apply(bind2nd(exponentiate(), power));
	{
		// Reduce (Column): pack along the columns, result is a vector of size n
		Dist::MPI_DenseVec colsums = A.Reduce(Column, plus<float>(), 0.0);
		colsums.Apply(safemultinv<float>());
		A.DimApply(Column, colsums, multiplies<float>());	// scale each "Column" with the given vector

#ifdef COMBBLAS_DEBUG
		colsums = A.Reduce(Column, plus<float>(), 0.0);
		colsums.PrintToFile("colnormalizedsums"); 
#endif		
	}

	// After normalization, each column of A is now a stochastic vector
	Dist::MPI_DenseVec colssqs = A.Reduce(Column, plus<float>(), 0.0, bind2nd(exponentiate(), 2));	// sums of squares of columns

	// Matrix entries are non-negative, so max() can use zero as identity
    Dist::MPI_DenseVec colmaxs = A.Reduce(Column, maximum<float>(), 0.0);

	colmaxs -= colssqs;	// chaos indicator
	return colmaxs.Reduce(maximum<float>(), 0.0);
}


int main(int argc, char* argv[])
{
	int nprocs, myrank;
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD,&nprocs);
	MPI_Comm_rank(MPI_COMM_WORLD,&myrank);
	typedef PlusTimesSRing<float, float> PTFF;
	if(argc < 5)
        {
		if(myrank == 0)
		{	
                	cout << "Usage: ./mcl <FILENAME_MATRIX_MARKET> <INFLATION> <PRUNELIMIT> <BASE_OF_MM>" << endl;
                	cout << "Example: ./mcl input.mtx 2 0.0001 0" << endl;
                }
		MPI_Finalize(); 
		return -1;
        }

	{
		float inflation = atof(argv[2]);
		float prunelimit = atof(argv[3]);

		string ifilename(argv[1]);		

		Dist::MPI_DCCols A;	// construct object
		if(string(argv[4]) == "0")
		{
            SpParHelper::Print("Treating input zero based\n");
            A.ParallelReadMM(ifilename, false, maximum<float>());	// use zero-based indexing for matrix-market file
		}
		else
		{
            A.ParallelReadMM(ifilename, true, maximum<float>());
		}
		
		SpParHelper::Print("File Read\n");
		float balance = A.LoadImbalance();
		int64_t nnz = A.getnnz();
		ostringstream outs;
		outs << "Load balance: " << balance << endl;
		outs << "Nonzeros: " << nnz << endl;
		SpParHelper::Print(outs.str());
        
		A.AddLoops(1.0);	// matrix_add_loops($mx); // with weight 1.0
        SpParHelper::Print("Added loops\n");
        
		Inflate(A, 1); 		// matrix_make_stochastic($mx);
        SpParHelper::Print("Made stochastic\n");

	
		// chaos doesn't make sense for non-stochastic matrices	
		// it is in the range {0,1} for stochastic matrices
		float chaos = 1;

		// while there is an epsilon improvement
		while( chaos > EPS)
		{
            A.PrintInfo();
			double t1 = MPI_Wtime();
			//A.Square<PTFF>() ;		// expand
            MemEfficientSpGEMM<PTFF, float, Dist::DCCols>(A, A, 1, prunelimit);
			
			chaos = Inflate(A, inflation);	// inflate (and renormalize)

			stringstream s;
			s << "New chaos: " << chaos << '\n';
			SpParHelper::Print(s.str());
			
#ifdef DEBUG	
			SpParHelper::Print("Before pruning...\n");
			A.PrintInfo();
#endif
			A.Prune(bind2nd(less<float>(), prunelimit));
			
			double t2=MPI_Wtime();
			if(myrank == 0)
				printf("%.6lf seconds elapsed for this iteration\n", (t2-t1));

#ifdef DEBUG	
			SpParHelper::Print("After pruning...\n");
			A.PrintInfo();
#endif
		}
		Interpret(A);	
	}	

	// make sure the destructors for all objects are called before MPI::Finalize()
	MPI_Finalize();	
	return 0;
}
