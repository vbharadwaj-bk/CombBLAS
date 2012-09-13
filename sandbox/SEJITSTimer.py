import time
time_very_beginning = time.time()

import sys
import os
import math
import random
import kdt
import kdt.pyCombBLAS as pcb
from stats import splitthousands, printstats

kdt.PDO_enable(False)


vecLength = 20 #10000000
totalRepeats = 5 #50
reuseRepeats = 5 #50

filterPercent = 10

# initialize the SEJITS routines

import pcb_predicate, pcb_function, pcb_function_sm as f_sm

#s2nd = pcb_function.PcbBinaryFunction(f_sm.BinaryFunction([f_sm.Identifier("x"), f_sm.Identifier("y")],
#									 f_sm.FunctionReturn(f_sm.Identifier("y"))),
#				 types=["double", "Obj2", "double"])
#s2nd_d = pcb_function.PcbBinaryFunction(f_sm.BinaryFunction([f_sm.Identifier("x"), f_sm.Identifier("y")],
#									   f_sm.FunctionReturn(f_sm.Identifier("y"))),
#				   types=["double", "double", "double"])
select1st_class = pcb_function.PcbBinaryFunction(f_sm.BinaryFunction([f_sm.Identifier("x"), f_sm.Identifier("y")],
									 f_sm.FunctionReturn(f_sm.Identifier("x"))),
				 types=["double", "double", "Obj2"])

select1st = select1st_class.get_function()
#func2 = s2nd_d.get_function()

#sejits_SR = kdt.sr(func2, func)

#s1st = pcb_function.PcbBinaryFunction(f_sm.BinaryFunction([f_sm.Identifier("x"), f_sm.Identifier("y")],
#									 f_sm.FunctionReturn(f_sm.Identifier("x"))),
#				 types=["double", "double", "double"]).get_function()


# the Twitter filter
from pcb_predicate import *

class TwitterFilter(PcbUnaryPredicate):
	def __init__(self, filterUpperValue):
		self.filterUpperValue = filterUpperValue
		super(TwitterFilter, self).__init__()
	def __call__(self, e):
		if (e.count > 0 and e.latest < self.filterUpperValue):
				return True
		else:
				return False

# create the operating vectors
def Twitter_obj_converter(obj, bin):
	obj.count = 1
	obj.latest = 1
	obj.follower = 0
	return obj

begin = time.time()
obj2Vec = kdt.Vec(vecLength, sparse=True, element=kdt.Obj2())
doubleVec = kdt.Vec.ones(vecLength, sparse=False)
obj2Vec.eWiseApply(doubleVec, op=Twitter_obj_converter, allowANulls=True, inPlace=True)
elapsed = time.time()-begin
kdt.p("created vectors of length %d in %f"%(vecLength, elapsed))


# create filterable fake twitter data
def Twitter_obj_randomizer_Apply(obj):
	obj.latest = int(float(pcb._random())*10000.0) #random.randrange(0, 10000)
	#print "rnd result:",obj.latest
	return obj

begin = time.time()
obj2Vec.apply(Twitter_obj_randomizer_Apply)
elapsed = time.time()-begin
kdt.p("randomized in %f"%(elapsed))


i = 1
# regular run
for tr in range(totalRepeats):
	before = time.time()
	select1st = select1st_class.get_function()
	kdt.p("Created SEJITS select1st bin function in %f for unfiltered #%d"%(time.time()-before, tr))
	for rr in range(reuseRepeats):
		before = time.time()
		doubleVec.eWiseApply(obj2Vec, op=select1st, inPlace=True)
		elapsed = time.time() - before
		kdt.p("%d\tunfiltered iteration\t%d\t%d\ttime:\t%s\t (vec length %d)"%(i, tr, rr, elapsed, vecLength))
		i += 1

# filtered run
for tr in range(totalRepeats):
	filterUpperValue = filterPercent*100
	before = time.time()
	sejits_filter = TwitterFilter(filterUpperValue).get_predicate()
	sejits_filter_create_time = time.time()-before
	kdt.p("Created SEJITS filter for \t%d\t%% in\t%f\ts."%(filterPercent, sejits_filter_create_time))
	
	obj2Vec.addFilter(sejits_filter)

	before = time.time()
	select1st = select1st_class.get_function()
	kdt.p("Created SEJITS select1st bin function in %f for unfiltered #%d"%(time.time()-before, tr))
	for rr in range(reuseRepeats):
		before = time.time()
		doubleVec.eWiseApply(obj2Vec, op=select1st, inPlace=True)
		elapsed = time.time() - before
		kdt.p("%d\tfiltered iteration\t%d\t%d\ttime:\t%s\t (vec length %d)"%(i, tr, rr, elapsed, vecLength))
		i += 1

	obj2Vec.delFilter(sejits_filter)