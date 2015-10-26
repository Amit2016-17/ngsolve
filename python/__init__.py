"""
NGSolve
=======

A high order finite element library

Modules:
ngsolve.bla .... simple vectors and matrices
ngsolve.fem .... finite elements and integrators
ngsolve.comp ... function spaces, forms
"""

from os import environ
from sys import path
path.append(environ['NETGENDIR']+'/../lib')

from sys import platform as __platform
if __platform.startswith('linux') or __platform.startswith('darwin'):
    path.append(environ['NETGENDIR']+'/../lib')
if __platform.startswith('win'):
    path.append(environ['NETGENDIR'])
    
del environ
del path

#old
# __all__ = ['ngstd','bla','fem','la','comp','solve','utils']


#new:
# from ngsolve.ngstd import *
# from ngsolve.bla import *
# from ngsolve.la import *
# from ngsolve.fem import *
# from ngsolve.comp import *
# from ngsolve.solve import *
# from ngsolve.utils import *

from ngslib import *

ngstd.__all__ = ['ArrayD', 'ArrayI', 'BitArray', 'Flags', 'HeapReset', 'IntRange', 'LocalHeap', 'Timers', 'RunWithTaskManager']
bla.__all__ = ['Matrix', 'Vector', 'InnerProduct']
la.__all__ = ['BaseMatrix', 'BaseVector', 'InnerProduct', 'CGSolver']
fem.__all__ =  ['BFI', 'CoefficientFunction',  'DomainConstantCF', 'CoordCF', 'ET', 'ElementTransformation', 'ElementTopology', 'FiniteElement', 'ScalarFE', 'H1FE', 'HEX', 'L2FE', 'LFI', 'POINT', 'PRISM', 'PYRAMID', 'PythonCF', 'QUAD', 'SEGM', 'TET', 'TRIG', 'VERTEX', 'EDGE', 'FACE', 'CELL', 'ELEMENT', 'FACET', 'VariableCF', 'SetPMLParameters', 'sin', 'cos', 'tan', 'exp', 'log', 'sqrt', 'Conj', 'specialcf', \
           'BlockBFI', 'BlockLFI', 'CompoundBFI', 'CompoundLFI', 'ConstantCF' \
           ]
comp.__all__ =  ['BND', 'BilinearForm', 'COUPLING_TYPE', 'CompoundFESpace', 'ElementId', 'BndElementId', 'FESpace', 'GridFunction', 'LinearForm', 'Mesh', 'Preconditioner', 'VOL', 'NumProc', 'PDE', 'PyNumProc', 'Integrate', 'SymbolicLFI', 'SymbolicBFI', 'SymbolicEnergy', 'VTKOutput', 'ngsglobals']           
solve.__all__ =  ['Redraw', 'BVP', 'CalcFlux', 'Draw', 'DrawFlux']

from ngsolve.ngstd import *
from ngsolve.bla import *
from ngsolve.la import *
from ngsolve.fem import *
from ngsolve.comp import *
from ngsolve.solve import *
from ngsolve.utils import *

__all__ = ngstd.__all__ + bla.__all__ +la.__all__ + fem.__all__ + comp.__all__ + solve.__all__ + utils.__all__




