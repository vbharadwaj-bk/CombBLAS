#include <mpi.h>
#include <sys/time.h> 
#include <iostream>
#include <functional>
#include <algorithm>
#include <vector>
#include <sstream>
#include <tr1/tuple>
#include "../SpParVec.h"
#include "../SpTuples.h"
#include "../SpDCCols.h"
#include "../SpParMat.h"
#include "../DenseParMat.h"
#include "../DenseParVec.h"
#include "../SpDefs.h"

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

	if(argc < 4)
	{
		if(myrank == 0)
		{
			cout << "Usage: ./ReduceTest <MatrixA> <SumColumns> <SumRows>" << endl;
			cout << "<Matrix>,<SumColumns>,<SumRows> are absolute addresses, and files should be in triples format" << endl;
		}
		MPI::Finalize(); 
		return -1;
	}				
	{
		string Aname(argv[1]);		
		string Bname(argv[2]);
		string Cname(argv[3]);

		ifstream inputA(Aname.c_str());
		ifstream inputB(Bname.c_str());
		ifstream inputC(Cname.c_str());

		MPI::COMM_WORLD.Barrier();
	
		PSpMat<double>::MPI_DCCols A;	
		DenseParVec<int,double> colsums(A.getcommgrid(), 0.0);
		DenseParVec<int,double> rowsums(A.getcommgrid(), 0.0);

		A.ReadDistribute(inputA, 0);
		colsums.ReadDistribute(inputB, 0);
		rowsums.ReadDistribute(inputC, 0);
		
		DenseParMat<int, double> bcu(0.0, A.getcommgrid(), A.getlocalrows(), A.getlocalcols() );
		bcu += A;

		DenseParVec< int, double > rowsums_control = bcu.Reduce(Column, std::plus<double>() , 0.0);
		DenseParVec< int, double > colsums_control = bcu.Reduce(Row, std::plus<double>() , 0.0);
		
		//ofstream rsout;
		//rowsums_control.PrintToFile("rowsums_countrol", rsout);	

		if (rowsums_control == rowsums && colsums_control == colsums)
		{
			SpParHelper::Print("Reduction via summation working correctly\n");	
		}
		else
		{
			SpParHelper::Print("ERROR in Reduce via summation, go fix it!\n");	
		}

		inputA.clear();
		inputA.close();
		inputB.clear();
		inputB.close();
		inputC.clear();
		inputC.close();
	}
	MPI::Finalize();
	return 0;
}


