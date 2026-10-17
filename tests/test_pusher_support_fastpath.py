#!/usr/bin/env python3
"""Deterministic octagonal support selection regression (Q8/Q12 proxy, not MCU FPS)."""
import math
import random

RX=(4096,2896,0,-2896,-4096,-2896,0,2896)
RY=(0,2896,4096,2896,0,-2896,-4096,-2896)
def shift(x):return x>>12

def axes(q):
 w,x,y,z=q
 return ((1-2*(y*y+z*z),2*(x*y+w*z),2*(x*z-w*y)),
         (2*(x*y-w*z),1-2*(x*x+z*z),2*(y*z+w*x)),
         (2*(x*z+w*y),2*(y*z-w*x),1-2*(x*x+y*y)))

def verify():
 rng=random.Random(17026);fallbacks=0;naive_mismatches=0;count=50000
 for _ in range(count):
  q=[rng.gauss(0,1) for _ in range(4)];length=math.sqrt(sum(v*v for v in q))
  basis=tuple(tuple(int(v*4096) for v in row) for row in axes([v/length for v in q]))
  normal=[rng.randrange(-4096,4097) for _ in range(3)]
  length=math.sqrt(sum(v*v for v in normal)) or 1
  normal=tuple(int(v*4096/length) for v in normal)
  x=shift(sum(normal[k]*basis[0][k] for k in range(3)))
  y=shift(sum(normal[k]*basis[1][k] for k in range(3)))
  approx=[];exact=[]
  for k in range(8):
   lx=shift(2048*RX[k]);ly=shift(2048*RY[k])
   approx.append(shift(x*lx+y*ly))
   point=[shift(basis[0][j]*lx)+shift(basis[1][j]*ly) for j in range(3)]
   exact.append(shift(sum(normal[j]*point[j] for j in range(3))))
  fast=max(range(8),key=lambda k:approx[k])
  reference=max(range(8),key=lambda k:exact[k])
  ordered=sorted(approx,reverse=True)
  borderline=ordered[0]-ordered[1]<=16
  selected=reference if borderline else fast
  assert selected==reference,(_ ,fast,reference,ordered)
  fallbacks+=borderline;naive_mismatches+=(fast!=reference)
 print(f"Octagon Q12 support: {count} poses PASS; {fallbacks} exact fallbacks ({fallbacks/count:.2%}), {naive_mismatches} approximate edge mismatches repaired.")

if __name__=="__main__":verify()
