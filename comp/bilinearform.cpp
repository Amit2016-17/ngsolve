#include <comp.hpp>
#include <multigrid.hpp>


namespace ngcomp
{
  using namespace ngcomp;

  // dummy function header 
  void CalcEigenSystem (FlatMatrix<Complex> & elmat, 
			FlatVector<> & lami, 
			FlatMatrix<> & evecs)
  { ; }



  BilinearForm :: 
  BilinearForm (const FESpace & afespace,
		const string & aname,
		const Flags & flags)
    : NGS_Object(afespace.GetMeshAccess(), aname), fespace(afespace)
  {
    fespace2 = NULL;
    diagonal = 0;
    multilevel = 1;
    galerkin = 0;
    symmetric = 1;
    hermitean = 0;
    nonassemble = 0;
    SetEpsRegularization (0);
    SetUnusedDiag (0);
    low_order_bilinear_form = 0;
    linearform = 0;
    timing = 0;
    print = 0;
    printelmat = 0;
    elmat_ev = 0;
    eliminate_internal = 0;
    keep_internal = 0;


    SetGalerkin( flags.GetDefineFlag( "project" ) );
    SetNonAssemble (flags.GetDefineFlag ("nonassemble"));
    // SetDiagonal (flags.GetDefineFlag ("diagonal"));
    if (flags.GetDefineFlag ("nonsym"))  SetSymmetric (0);
    if (flags.GetDefineFlag ("nonmultilevel")) SetMultiLevel (0);
    SetHermitean (flags.GetDefineFlag ("hermitean"));
    SetUnusedDiag (flags.GetNumFlag ("unuseddiag",1));
    SetEpsRegularization (flags.GetNumFlag ("regularization",0));
  
    SetPrint (flags.GetDefineFlag ("print"));
    SetPrintElmat (flags.GetDefineFlag ("printelmat"));
    SetElmatEigenValues (flags.GetDefineFlag ("elmatev")); 
    if (flags.GetDefineFlag ("timing")) SetTiming (1);
    if (flags.GetDefineFlag ("eliminate_internal")) SetEliminateInternal (1);
    if (flags.GetDefineFlag ("keep_internal")) SetKeepInternal (1);
    if (flags.GetDefineFlag ("store_inner")) SetStoreInner (1);

    precompute = flags.GetDefineFlag ("precompute");
  }


  BilinearForm :: 
  BilinearForm (const FESpace & afespace,
		const FESpace & afespace2, const string & aname,
		const Flags & flags)
    : NGS_Object(afespace.GetMeshAccess(), aname), fespace(afespace), fespace2(&afespace2)
  {
    diagonal = 0;
    multilevel = 1;
    galerkin = 0;
    symmetric = 1;
    hermitean = 0;
    nonassemble = 0;
    SetEpsRegularization (0);
    SetUnusedDiag (0);
    low_order_bilinear_form = 0;
    linearform = 0;
    timing = 0;
    print = 0;
    printelmat = 0;
    elmat_ev = 0;
    eliminate_internal = 0;
    keep_internal = 0;


    SetGalerkin( flags.GetDefineFlag( "project" ) );
    SetNonAssemble (flags.GetDefineFlag ("nonassemble"));
    // SetDiagonal (flags.GetDefineFlag ("diagonal"));
    if (flags.GetDefineFlag ("nonsym"))  SetSymmetric (0);
    if (flags.GetDefineFlag ("nonmultilevel")) SetMultiLevel (0);
    SetHermitean (flags.GetDefineFlag ("hermitean"));
    SetUnusedDiag (flags.GetNumFlag ("unuseddiag",1));
  
    SetPrint (flags.GetDefineFlag ("print"));
    SetPrintElmat (flags.GetDefineFlag ("printelmat"));
    SetElmatEigenValues (flags.GetDefineFlag ("elmatev"));

 
    if (flags.GetDefineFlag ("timing")) SetTiming (1);
    if (flags.GetDefineFlag ("eliminate_internal")) SetEliminateInternal (1);
    if (flags.GetDefineFlag ("keep_internal")) SetKeepInternal (1);
    if (flags.GetDefineFlag ("store_inner")) SetStoreInner (1);

    precompute = flags.GetDefineFlag ("precompute");
  }


  
  void BilinearForm :: AddIntegrator (BilinearFormIntegrator * bfi, const bool deletable)
  {
    parts.Append (bfi);
    parts_deletable.Append(deletable);
    if (low_order_bilinear_form)
      low_order_bilinear_form -> AddIntegrator (parts.Last(),false);
  }


  void BilinearForm :: AddIndependentIntegrator (BilinearFormIntegrator * bfi,
						 const int master_surface,
						 const int slave,
						 const bool deletable)
  {
    independent_parts.Append (bfi);
    independent_parts_deletable.Append(deletable);
    Vec<2,int> indices(master_surface,slave); 
    independent_meshindex.Append(indices);
    if (low_order_bilinear_form)
      low_order_bilinear_form -> AddIndependentIntegrator (independent_parts.Last(),
							   master_surface,slave,
							   deletable);
  }


  BilinearForm :: ~BilinearForm ()
  {
    if (!low_order_bilinear_form)
      for (int i = 0; i < parts.Size(); i++)
	if(parts_deletable[i]) delete parts[i];
	
    //    for (int i = 0; i < precomputed_data.Size(); i++)
    //      delete precomputed_data[i];

    delete low_order_bilinear_form;
  }

  void BilinearForm :: SetPrint (bool ap)
  { 
    print = ap; 
    if (low_order_bilinear_form)
      low_order_bilinear_form -> SetPrint (ap);
  }

  void BilinearForm :: SetPrintElmat (bool ap)
  { 
    printelmat = ap; 
    if (low_order_bilinear_form)
      low_order_bilinear_form -> SetPrintElmat (ap);
  }

  void BilinearForm ::  SetElmatEigenValues (bool ee)
  { 
    elmat_ev = ee;
    if (low_order_bilinear_form)
      low_order_bilinear_form -> SetElmatEigenValues (ee);
  }




  MatrixGraph * BilinearForm :: GetGraph (int level, bool symmetric)
  {
    static Timer timer ("BilinearForm::GetGraph");
    RegionTimer reg (timer);

    int ndof = fespace.GetNDof();
    int ne = GetMeshAccess().GetNE();
    int nse = GetMeshAccess().GetNSE();
    const Array<SpecialElement*> & specialelements = fespace.GetSpecialElements();
    
    Array<int> dnums;
    Array<int> fnums; //facets of one element
    Array<int> elnums; //elements neighbouring one facet
    Array<int> nbelems; //neighbour elements



    TableCreator<int> creator;
    for ( ; !creator.Done(); creator++)
      {
	for (int i = 0; i < ne; i++)
	  {
	    if ( ma.IsGhostEl(i)) continue;
	    if (!fespace.DefinedOn (ma.GetElIndex(i))) continue;

	    if (eliminate_internal)
	      fespace.GetDofNrs (i, dnums, EXTERNAL_DOF);
	    else
	      fespace.GetDofNrs (i, dnums);

	    for (int j = 0; j < dnums.Size(); j++)
	      if (dnums[j] != -1)
		creator.Add (i, dnums[j]);
	  }
	
	
	if (fespace.UsesDGCoupling())
	  //add dofs of neighbour elements as well
	  for (int i = 0; i < ne; i++)
	    {
	      nbelems.SetSize(0);
	      ma.GetElFacets(i,fnums);

	      for (int j=0; j<fnums.Size();j++)
		{
		  ma.GetFacetElements(fnums[j],elnums);
		  for (int k=0; k<elnums.Size(); k++)
		    if(elnums[k]!=i) nbelems.Append(elnums[k]);
		}

	      for (int k=0;k<nbelems.Size();k++){
		int elnr=nbelems[k];
		if ( ma.IsGhostEl(elnr)) continue;
		if (!fespace.DefinedOn (ma.GetElIndex(elnr))) continue;
		fespace.GetDofNrs (elnr, dnums);
		for (int j = 0; j < dnums.Size(); j++)
		  if (dnums[j] != -1)
		    creator.Add (i, dnums[j]);
	      }
	    }

	for (int i = 0; i < nse; i++)
	  {
	    if ( ma.IsGhostSEl(i)) continue;
	    if (!fespace.DefinedOnBoundary (ma.GetSElIndex(i))) continue;
	    
	    fespace.GetSDofNrs (i, dnums);
	    for (int j = 0; j < dnums.Size(); j++)
	      if (dnums[j] != -1)
		creator.Add (ne+i, dnums[j]);
	  }


	for (int i = 0; i < specialelements.Size(); i++)
	  {
	    specialelements[i]->GetDofNrs (dnums);
	    
	    for (int j = 0; j < dnums.Size(); j++)
	      if (dnums[j] != -1)
		creator.Add (ne+nse+i, dnums[j]);
	  }
      }

    MatrixGraph * graph = new MatrixGraph (ndof, *creator.GetTable(), *creator.GetTable(), symmetric);

    graph -> FindSameNZE();

    return graph;
  }



#ifdef PARALLEL
  MatrixGraph * BilinearForm :: GetConsistentGraph (int level, bool symmetric)
  {
    int j, k;
    int ndof = fespace.GetNDof();
    int ne = GetMeshAccess().GetNE();
    int nse = GetMeshAccess().GetNSE();

    Array<int> linesize (ndof);
    Array<int> dnums;

    Array<int> dof_num_el (ndof);
    Array<int> el_num_dof (ne);

    PrintReport (*testout);

    // generate dof -> volume element table
    dof_num_el = 0;
    el_num_dof = 0;


    for (int i = 0; i < ne; i++)
      {
// 	if (  !ma.IsExchangeEl(i)) continue;
	if (!fespace.DefinedOn (ma.GetElIndex(i))) continue;
	if (eliminate_internal)
	  fespace.GetDofNrs (i, dnums, EXTERNAL_DOF);
	else
	  fespace.GetDofNrs (i, dnums);
	for (j = 0; j < dnums.Size(); j++)
	  if (dnums[j] != -1)
	    if ( fespace.GetParallelDofs().IsExchangeDof(dnums[j]) ) 
	      {
		dof_num_el[dnums[j]]++;
		el_num_dof[i]++;
	      }
      }
    
    
    Table<int> dof2el (dof_num_el);
    Table<int> el2dof (el_num_dof);

    dof_num_el = 0;
    el_num_dof = 0;
    for (int i = 0; i < ne; i++)
      {
// 	if (  !ma.IsExchangeEl(i)) continue;
	if (!fespace.DefinedOn (ma.GetElIndex(i))) continue;
	if (eliminate_internal)
	  fespace.GetDofNrs (i, dnums, EXTERNAL_DOF);
	else
	  fespace.GetDofNrs (i, dnums);
	    
	for (j = 0; j < dnums.Size(); j++)
	  if (dnums[j] != -1)
	    if ( fespace.GetParallelDofs().IsExchangeDof(dnums[j]) ) 
	      {
		dof2el[dnums[j]][dof_num_el[dnums[j]]++] = i;
		el2dof[i][el_num_dof[i]++] = dnums[j];
	      }
      }

    // generate dof -> surface element table
    dof_num_el = 0;
    for (int i = 0; i < nse; i++)
      {
// 	if ( !ma.IsExchangeSEl (i) ) continue;
	fespace.GetSDofNrs (i, dnums);

	for (j = 0; j < dnums.Size(); j++)
	  if (dnums[j] != -1)
	    if ( fespace.GetParallelDofs().IsExchangeDof(dnums[j]) ) 
	      dof_num_el[dnums[j]]++;
      }

    Table<int> dof2sel (dof_num_el);

    dof_num_el = 0;
    for (int i = 0; i < nse; i++)
      {
	// if ( !ma.IsExchangeSEl (i) ) continue;
	if (!fespace.DefinedOnBoundary (ma.GetSElIndex(i))) continue;
	fespace.GetSDofNrs (i, dnums);
	    
	for (j = 0; j < dnums.Size(); j++)
	  if (dnums[j] != -1)
	    if ( fespace.GetParallelDofs().IsExchangeDof(dnums[j]) )
	      {
		dof2sel[dnums[j]][dof_num_el[dnums[j]]] = i;
		dof_num_el[dnums[j]]++;
	      }
      }

     


    Array<int> elflags(ndof);
    elflags = -1;

    for (int i = 0; i < ndof; i++)
      {
	linesize[i] = 1;
	elflags[i] = i;
	for (j = 0; j < dof2el[i].Size(); j++)
	  {
	    int elnr = dof2el[i][j];

            /*
	    GetDofNrs (elnr, dnums);
	    for (k = 0; k < dnums.Size(); k++)
	      if (dnums[k] != -1 &&
		  (!symmetric || dnums[k] <= i))
		{
		  if (elflags[dnums[k]] != i)
		    {
		      elflags[dnums[k]] = i;
		      linesize[i]++;
		    }
		}
            */

	    FlatArray<int> dnums2 = el2dof[elnr];
	    for (k = 0; k < dnums2.Size(); k++)
	      {
		int dnumk = dnums2[k];
		if (!symmetric || dnumk <= i)
		  {
		    if (elflags[dnumk] != i)
		      {
			elflags[dnumk] = i;
			linesize[i]++;
		      }
		  }
	      }
	  }

	for (j = 0; j < dof2sel[i].Size(); j++)
	  {
	    int elnr = dof2sel[i][j];
	    fespace.GetSDofNrs (elnr, dnums);
	    for (k = 0; k < dnums.Size(); k++)
	      if (dnums[k] != -1 &&
		  (!symmetric || dnums[k] <= i))
		{
		  if (elflags[dnums[k]] != i)
		    {
		      elflags[dnums[k]] = i;
		      linesize[i]++;
		    }
		}
	  }


      }

    int cnt = 0;
    for (int l = 0; l < linesize.Size(); l++)
      cnt += linesize[l];

    MatrixGraph * graph = new MatrixGraph (linesize);

    //      graph->Print (cout);

    Array<int> help(ndof);
    elflags = -1;
      
    for (int i = 0; i < ndof; i++)
      {
	int * data = graph->GetRowIndicesPointer(i);
	*data = i;
	data++;
	elflags[i] = i;

	for (j = 0; j < dof2el[i].Size(); j++)
	  {
	    int elnr = dof2el[i][j];
	    /*
	    GetDofNrs (elnr, dnums);
	    for (k = 0; k < dnums.Size(); k++)
	      if (dnums[k] != -1 &&
		  (!symmetric || dnums[k] <= i))
		{
		  if (elflags[dnums[k]] != i)
		    {
		      elflags[dnums[k]] = i;
		      *data = dnums[k];
		      data++;
		    }
		}
	    */

	    FlatArray<int> dnums2 = el2dof[elnr];
	    for (k = 0; k < dnums2.Size(); k++)
	      {
		int dnumk = dnums2[k];
		if (!symmetric || dnumk <= i)
		  {
		    if (elflags[dnumk] != i)
		      {
			elflags[dnumk] = i;
			*data = dnumk;
			data++;
		      }
		  }
	      }
	  }
	  
	for (j = 0; j < dof2sel[i].Size(); j++)
	  {
	    int elnr = dof2sel[i][j];
	    fespace.GetSDofNrs (elnr, dnums);
	    for (k = 0; k < dnums.Size(); k++)
	      if (dnums[k] != -1 &&
		  (!symmetric || dnums[k] <= i))
		{
		  if (elflags[dnums[k]] != i)
		    {
		      elflags[dnums[k]] = i;
		      *data = dnums[k];
		      data++;
		    }
		}
	  }
	  


	MergeSort (linesize[i], graph->GetRowIndicesPointer(i), &help[0]);
      }

    graph -> FindSameNZE();
    return graph;
  }
#endif




  void BilinearForm :: Assemble (LocalHeap & lh)
  {
    if (mats.Size() == ma.GetNLevels())
      return;


    if (nonassemble)
      {
	mats.Append (new BilinearFormApplication (this)); 
      
	if (precompute)
	  {
	    precomputed_data.SetSize(0);

	    Array<int> dnums;
	    ElementTransformation eltrans;
	    
	    int ne = ma.GetNE();
	    int nse = ma.GetNSE();
      
	    LocalHeap lh (20000000, "biform - assemble");
	    
	    int hasbound = 0;
	    int hasinner = 0;
	    
	    for (int j = 0; j < NumIntegrators(); j++)
	      {
		const BilinearFormIntegrator & bfi = *GetIntegrator(j);
		if (bfi.BoundaryForm())
		  hasbound = 1;
		else
		  hasinner = 1;
	      }
	    
	    
	    if (hasinner)
	      for (int i = 0; i < ne; i++)
		{
		  lh.CleanUp();
		  
		  if (!fespace.DefinedOn (ma.GetElIndex (i))) continue;
	  
		  const FiniteElement & fel = fespace.GetFE (i, lh);
		  ma.GetElementTransformation (i, eltrans, lh);
		  fespace.GetDofNrs (i, dnums);
	  
		  for (int j = 0; j < NumIntegrators(); j++)
		    {
		      const BilinearFormIntegrator & bfi = *parts[j];
		      
		      if (bfi.BoundaryForm()) continue;
		      if (!bfi.DefinedOn (ma.GetElIndex (i))) continue;
		      
		      precomputed_data.Append (bfi.PrecomputeData (fel, eltrans, lh));
		    }
		}


	    if (hasbound)
	      for (int i = 0; i < nse; i++)
		{
		  lh.CleanUp();
		  
		  const FiniteElement & fel = fespace.GetSFE (i, lh);
		  ma.GetSurfaceElementTransformation (i, eltrans, lh);
		  fespace.GetSDofNrs (i, dnums);
	  
		  for (int j = 0; j < NumIntegrators(); j++)
		    {
		      const BilinearFormIntegrator & bfi = *parts[j];
		      
		      if (!bfi.BoundaryForm()) continue;
		      precomputed_data.Append (bfi.PrecomputeData (fel, eltrans, lh));
		    }
		}
	  }


	
	if (timing)
	  {
	    clock_t starttime;
	    double time;

	    starttime = clock();
	  
	    BaseVector & vecf = *mats.Last()->CreateVector();
	    BaseVector & vecu = *mats.Last()->CreateVector();
	  
	    vecu = 1;
	    int steps = 0;
	    do
	      {
		vecf = (*mats.Last()) * vecu;
		steps++;
		time = double(clock() - starttime) / CLOCKS_PER_SEC;
	      }
	    while (time < 2.0);
	  
	    cout << " 1 application takes "
		 << time / steps
		 << " seconds" << endl;

	  }


	return;
      }

	  try
	  {
    if (low_order_bilinear_form)
	{
	  low_order_bilinear_form->Assemble(lh);
	}
	  }
    catch (Exception & e)
      {
	e.Append (string ("\nthrown by Do loworder ") +
		  string (GetName()));
	throw e;
      }


    try
      {
	AllocateMatrix ();
      }
    catch (exception & e)
      {
	throw Exception (e.what() + 
			 string ("\nthrown by allocate matrix ") +
			 string (GetName()));
      }
    catch (Exception & e)
      {
	e.Append (string ("\nthrown by allocate matrix ") +
		  string (GetName()));
	throw;
      }



#ifdef PARALLEL
    // for ho processes, allocate matrix for storing the accumulated matrix
    AllocateConsistentMatrix();
#endif

    DoAssemble(lh);


    if (timing)
      {
	clock_t starttime;
	double time;
	starttime = clock();
      
	BaseVector & vecf = *mats.Last()->CreateVector();
	BaseVector & vecu = *mats.Last()->CreateVector();
      
	vecu = 1;
	int steps = 0;
	do
	  {
	    vecf = (*mats.Last()) * vecu;
	    steps++;
	    time = double(clock() - starttime) / CLOCKS_PER_SEC;
	  }
	while (time < 1.0);
	  
	cout << " 1 application takes "
	     << time / steps
	     << " seconds" << endl;

	int nze = dynamic_cast<const BaseSparseMatrix &> (*mats.Last()) . NZE();
	cout << "NZE = " << nze << ", MFLOP = " << double (nze * steps) / time * 1e-6 << endl;
	cout << "type = " << typeid(*mats.Last()).name() << endl;
      }

    if (galerkin)
      GalerkinProjection();
  }


  void BilinearForm :: ReAssemble (LocalHeap & lh, bool reallocate)
  {
    if (nonassemble) return;

    if (low_order_bilinear_form)
      low_order_bilinear_form->ReAssemble(lh);

    if (mats.Size() < ma.GetNLevels())
      {
	Assemble(lh);
	return;
      }


    if (reallocate)
      {
	delete mats.Last();
	// delete graphs.Last();
	mats.DeleteLast();
	// graphs.DeleteLast();

	Assemble(lh);
	return;
      }

    GetMatrix() = 0.0;
    DoAssemble(lh);

    if (galerkin)
      GalerkinProjection();
  }



  void BilinearForm :: PrintReport (ostream & ost)
  {
    ost << "on space " << GetFESpace().GetName() << endl
	<< "symmetric   = " << symmetric << endl
	<< "multilevel  = " << multilevel << endl
	<< "nonassemble = " << nonassemble << endl
	<< "printelmat = " << printelmat << endl
	<< "eliminate_internal = " << eliminate_internal << endl
	<< "keep_internal = " << keep_internal << endl
	<< "store_inner = " << store_inner << endl
	<< "integrators: " << endl;
  
    for (int i = 0; i < parts.Size(); i++)
      ost << "  " << parts[i]->Name() << endl;
  }


  void BilinearForm :: MemoryUsage (Array<MemoryUsageStruct*> & mu) const
  {
    if (low_order_bilinear_form)
      low_order_bilinear_form -> MemoryUsage (mu);
  
    int olds = mu.Size();

    for (int i = 0; i < mats.Size(); i++)
      if (mats[i]) mats[i]->MemoryUsage (mu);
    //  for (i = 0; i < graphs.Size(); i++)
    //    if (graphs[i]) graphs[i]->MemoryUsage (mu);

    for (int i = olds; i < mu.Size(); i++)
      mu[i]->AddName (string(" bf ")+GetName());
  }


  template <class SCAL>
  S_BilinearForm<SCAL> :: ~S_BilinearForm()
  {
    ;
  }




  template <class SCAL>
  void S_BilinearForm<SCAL> :: DoAssemble (LocalHeap & clh)
  {
    static Timer mattimer("Matrix assembling");

    static Timer timer1 ("Matrix assembling - 1");
    static Timer timer2 ("Matrix assembling - 2");
    static int timer3 = NgProfiler::CreateTimer ("Matrix assembling - 3");
    
    static int timerb1 = NgProfiler::CreateTimer ("Matrix assembling bound - 1");
    static int timerb2 = NgProfiler::CreateTimer ("Matrix assembling bound - 2");
    static int timerb3 = NgProfiler::CreateTimer ("Matrix assembling bound - 3");

    RegionTimer reg (mattimer);
    


    // check if integrators fit to space
    for (int i = 0; i < NumIntegrators(); i++)
      if (parts[i] -> BoundaryForm())
        {
          for (int j = 0; j < ma.GetNSE(); j++)
            {
              HeapReset hr(clh);
	      if (parts[i] -> DefinedOn (ma.GetSElIndex(j)))
		parts[i] -> CheckElement (fespace.GetSFE(j, clh));
	    }
        }
      else if (parts[i] -> SkeletonForm() && !parts[i] -> BoundaryForm()) 
	{ 
	  if (!fespace.UsesDGCoupling()) throw Exception("FESpace is not suitable for those integrators (try -dgjumps)");
	  //TODO: Check each facet for the neighbouring elements - necessary?
	}
      else
	{
	  for (int j = 0; j < ma.GetNE(); j++)
	    {
	      HeapReset hr(clh);
	      if (parts[i] -> DefinedOn (ma.GetElIndex(j)))
		parts[i] -> CheckElement (fespace.GetFE(j, clh));
	    }
	}
    
    
    try
      {
	if (!MixedSpaces())
	
	  {
	    ma.PushStatus ("Assemble Matrix");
 
	    Array<int> dnums, idofs, idofs1, odofs;

	    int ndof = fespace.GetNDof();
	    BitArray useddof(ndof);
	    useddof.Clear();

	    int ne = ma.GetNE();
	    int nse = ma.GetNSE();
	    int nf = ma.GetNFacets();

	    BaseMatrix & mat = GetMatrix();
	    mat = 0.0;

	    bool hasbound = false;
	    bool hasinner = false;
	    bool hasskeletonbound = false;
	    bool hasskeletoninner = false;
 
	    for (int j = 0; j < NumIntegrators(); j++)
	      {
		if (parts[j] -> BoundaryForm())
		  if (parts[j] -> SkeletonForm())
		    hasskeletonbound = true;
		  else
		    hasbound = true;
		else
		  if (parts[j] -> SkeletonForm())
		    hasskeletoninner = true;
		  else
		    hasinner = true;
	      }

	    *testout << " BILINEARFORM TEST:" << endl;
	    *testout << " hasinner = " << hasinner << endl;
	    *testout << " hasouter = " << hasbound << endl;
	    *testout << " hasskeletoninner = " << hasskeletoninner << endl;
	    *testout << " hasskeletonouter = " << hasskeletonbound << endl;
	    int nrcases = 0;
	    int loopsteps = 0;
	    if (hasinner) {nrcases++; loopsteps+=ne;}
	    if (hasbound) {nrcases++; loopsteps+=nse;}
	    if (hasskeletoninner) {nrcases++; loopsteps+=nf;}
	    if (hasskeletonbound) {nrcases++; loopsteps+=nse;}
	    if (fespace.specialelements.Size()>0) {nrcases++; loopsteps+=fespace.specialelements.Size();}
	    int actcase = 0;
	    int gcnt = 0; //global count (for all cases)
	    clock_t prevtime = clock();


	    if (hasinner && !diagonal)
	      {
		actcase++;
		int cnt = 0;
		  
		const Table<int> * element_coloring = &fespace.ElementColoring();
		int ncolors = (element_coloring) ? element_coloring->Size() : 1;

		if (eliminate_internal&&keep_internal)
		  {
		    harmonicext = new ElementByElementMatrix<SCAL>(ndof, ne);
		    if (!symmetric)
		      harmonicexttrans = new ElementByElementMatrix<SCAL>(ndof, ne);
		    else
		      harmonicexttrans = new Transpose(*harmonicext);
		    innersolve = new ElementByElementMatrix<SCAL>(ndof, ne);
		    if (store_inner)
		      innermatrix = new ElementByElementMatrix<SCAL>(ndof, ne);
		  }
		
		for (int icol = 0; icol < ncolors; icol++)
		  {
#pragma omp parallel 
		    {
		      LocalHeap lh = clh.Split();
		      
		      Array<int> dnums, idofs, idofs1, odofs;
		      
		      int nec = (element_coloring) ? (*element_coloring)[icol].Size() : ne;
		      
#pragma omp for 
		      for (int ii = 0; ii < nec; ii++)
			{
			  int i = (element_coloring) ? (*element_coloring)[icol][ii] : ii;
			  
			  
			  timer1.Start();
			  
			  HeapReset hr (lh);
			  
			  if(elmat_ev) *testout << " Assemble Element " << i << endl;  
			  

#pragma omp atomic
			  cnt++;

#pragma omp atomic
			  gcnt++;



			  if (clock()-prevtime > 0.1 * CLOCKS_PER_SEC)
			    {
#pragma omp critical(printmatasstatus)
			      {
				cout << "\rassemble element " << cnt << "/" << ne << flush;
				ma.SetThreadPercentage ( 100.0*gcnt / (loopsteps) );
				prevtime = clock();
			      }
			    }


			  if (ma.IsGhostEl(i)) continue;
			  if (!fespace.DefinedOn (ma.GetElIndex (i))) continue;


			  const FiniteElement & fel = fespace.GetFE (i, lh);
			  ElementTransformation eltrans = ma.GetTrafo (i, 0);
			  fespace.GetDofNrs (i, dnums);

			  if (fel.GetNDof() != dnums.Size())
			    {
			      cout << "fel:GetNDof() = " << fel.GetNDof() << endl;
			      cout << "dnums.Size() = " << dnums.Size() << endl;

			      (*testout) << "fel:GetNDof() = " << fel.GetNDof() << endl;
			      (*testout) << "dnums.Size() = " << dnums.Size() << endl;
			      (*testout) << "dnums = " << dnums << endl;
			      throw Exception ( "Inconsistent number of degrees of freedom " );
			    }

			  int elmat_size = dnums.Size()*fespace.GetDimension();
			  FlatMatrix<SCAL> sum_elmat(elmat_size, lh);
			  sum_elmat = 0;
                      
			  timer1.Stop();
			  NgProfiler::StartTimer (timer2);

			  for (int j = 0; j < NumIntegrators(); j++)
			    {
			      HeapReset hr (lh);
			      BilinearFormIntegrator & bfi = *parts[j];
		      
			      if (bfi.SkeletonForm()) continue;
			      if (bfi.BoundaryForm()) continue;
			      if (!bfi.DefinedOn (ma.GetElIndex (i))) continue;
		      
			      FlatMatrix<SCAL> elmat(elmat_size, lh);

			      try
				{
				  static int elementtimer = NgProfiler::CreateTimer ("Element matrix integration");
				  NgProfiler::StartTimer (elementtimer);
 
				  if (!diagonal)
				    bfi.CalcElementMatrix (fel, eltrans, elmat, lh);
				  else
				    {
				      FlatVector<double> diag;
				      bfi.CalcElementMatrixDiag (fel, eltrans, diag, lh);
				      elmat = 0.0;
				      for (int k = 0; k < diag.Size(); k++)
					elmat(k,k) = diag(k);
				    }

				  NgProfiler::StopTimer (elementtimer);
			  
				  if (printelmat)
				    {
				      testout->precision(8);
				      (*testout) << "elnum = " << i << endl;
				      (*testout) << "eltype = " << fel.ElementType() << endl;
				      (*testout) << "integrator = " << bfi.Name() << endl;
				      (*testout) << "dnums = " << endl << dnums << endl;
				      (*testout) << "elmat = " << endl << elmat << endl;
				    }


				  if (elmat_ev)
				    {
#ifdef LAPACK
				      LapackEigenSystem(elmat, lh);
#else
				      Vector<> lami(elmat.Height());
				      Matrix<> evecs(elmat.Height());
			      
				      CalcEigenSystem (elmat, lami, evecs);
				      (*testout) << "lami = " << endl << lami << endl;
#endif
				    } 
			  
				}
			      catch (Exception & e)
				{
				  e.Append (string("in Assemble Element Matrix, bfi = ") + 
					    bfi.Name() + string("\n"));

				  throw;
				}
			      catch (exception & e)
				{
				  throw (Exception (string(e.what()) +
						    string("in Assemble Element Matrix, bfi = ") + 
						    bfi.Name() + string("\n")));
				}
		      
			      sum_elmat += elmat;
			    }



			  NgProfiler::StopTimer (timer2);
			  NgProfiler::StartTimer (timer3);

			  fespace.TransformMat (i, false, sum_elmat, TRANSFORM_MAT_LEFT_RIGHT);


			  if (elmat_ev)
			    {
			      (*testout) << "sum matrix:" << endl;
#ifdef LAPACK
			      LapackEigenSystem(sum_elmat, lh);
#else
			      Vector<> lami(sum_elmat.Height());
			      Matrix<> evecs(sum_elmat.Height());
			
			      CalcEigenSystem (sum_elmat, lami, evecs);
			      (*testout) << "lami = " << endl << lami << endl ;
#endif
			    }



			  if (eliminate_internal)
			    {
			      static int statcondtimer = NgProfiler::CreateTimer ("static condensation");
			      NgProfiler::StartTimer (statcondtimer);


			      // fel.GetInternalDofs(idofs1);
			      fespace.GetDofNrs (i, idofs1, LOCAL_DOF);
			      for (int j = 0; j < idofs1.Size(); j++)
				for (int k = 0; k < dnums.Size(); k++)
				  if (dnums[k] == idofs1[j])
				    {
				      idofs1[j] = k; 
				      break;
				    }

			      if (printelmat) 
				{
				  *testout << "eliminate internal" << endl;
				  *testout << "idofs1 = " << idofs1 << endl;
				}
			      
			      


			      if (idofs1.Size())
				{
				  HeapReset hr (lh);

				  int size = sum_elmat.Height();
				  int dim = size / dnums.Size();

				  idofs.SetSize (0);
				  for (int j = 0; j < idofs1.Size(); j++)
				    for (int jj = 0; jj < dim; jj++)
				      idofs.Append (dim*idofs1[j]+jj);

				  *testout << "idofs = " << idofs << endl;

				  int sizei = idofs.Size();
				  int sizeo = size - sizei;

				  odofs.SetSize (0);
				  for (int j = 0; j < size; j++)
				    {
				      bool idof = 0;
				      for (int k = 0; k < idofs.Size(); k++)
					if (idofs[k] == j) idof = 1;
				      if (!idof) odofs.Append (j);
				    }

				  if (printelmat)
				    {
				      // (*testout) << "idofs = " << endl << idofs << endl;
				      (*testout) << "odofs = " << endl << odofs << endl;
				    }

				  FlatMatrix<SCAL> a(sizeo, sizeo, lh);
				  FlatMatrix<SCAL> b(sizeo, sizei, lh);
				  FlatMatrix<SCAL> c(sizeo, sizei, lh);
				  FlatMatrix<SCAL> d(sizei, sizei, lh);
			    
				  for (int k = 0; k < sizeo; k++)
				    for (int l = 0; l < sizeo; l++)
				      a(k,l) = sum_elmat(odofs[k], odofs[l]);
			    
				  for (int k = 0; k < odofs.Size(); k++)
				    for (int l = 0; l < idofs.Size(); l++)
				      {
					b(k,l) = sum_elmat(odofs[k], idofs[l]);
					c(k,l) = sum_elmat(idofs[l], odofs[k]);
				      }

				  for (int k = 0; k < idofs.Size(); k++)
				    for (int l = 0; l < idofs.Size(); l++)
				      d(k,l) = sum_elmat(idofs[k], idofs[l]);
			    
#ifdef LAPACK
				  /*
				    LapackInverse (d);
				    LapackMultABt (c, d, idc);
				    LapackMultAddABt (b, idc, -1, a);
				  */

				  NgProfiler::AddFlops (statcondtimer, double(sizei)*sizei*sizei/3);  // LU fact
				  NgProfiler::AddFlops (statcondtimer, double(sizei)*sizei*sizeo);  
				  NgProfiler::AddFlops (statcondtimer, double(sizei)*sizeo*sizeo);  
				  
				  
				  // A := A - B D^{-1} C^T
				  // new Versions, July 07
				  if (!keep_internal){
				    LapackAInvBt (d, b);
				    LapackMultAddABt (b, c, -1, a);
				  }
				  else
				  {
				    Array<int> idnums;
				    Array<int> ednums;
				    fespace.GetDofNrs(i,idnums,LOCAL_DOF);
				    fespace.GetDofNrs(i,ednums,EXTERNAL_DOF);
				    if (store_inner)
				      static_cast<ElementByElementMatrix<SCAL>*>(innermatrix)->AddElementMatrix(i,idnums,idnums,d);
				    LapackInverse (d);
				    FlatMatrix<SCAL> he (sizei, sizeo, lh);
				    he=0.0;
				    LapackMultAddABt (d, c, -1, he);
				    // 				    he = -1.0 * d * Trans(c);
				    static_cast<ElementByElementMatrix<SCAL>*>(harmonicext)->AddElementMatrix(i,idnums,ednums,he);
				    
				    if (!symmetric){
				      FlatMatrix<SCAL> het (sizeo, sizei, lh);
				      het = 0.0;
				      LapackMultAddAB (b, d, -1, het);
// 				      het = -1.0 * b * d;
				      static_cast<ElementByElementMatrix<SCAL>*>(harmonicexttrans)->AddElementMatrix(i,ednums,idnums,het);
				    }
				    static_cast<ElementByElementMatrix<SCAL>*>(innersolve)->AddElementMatrix(i,idnums,idnums,d);
				    
				    LapackMultAddAB (b, he, 1.0, a);
				  }				  
			    
#else
				  FlatMatrix<SCAL> invd(sizei, sizei, lh);
				  FlatMatrix<SCAL> idc(sizeo, sizei, lh);
				  CalcInverse (d, invd);
				  if (keep_internal) 
				  { 
				    Array<int> idnums;
				    Array<int> ednums;
				    fespace.GetDofNrs(i,idnums,LOCAL_DOF);
				    fespace.GetDofNrs(i,ednums,EXTERNAL_DOF);
				    if (store_inner)
				      static_cast<ElementByElementMatrix<SCAL>*>(innermatrix)->AddElementMatrix(i,idnums,idnums,d);
				    d = invd;
				    FlatMatrix<SCAL> he (sizei, sizeo, lh);
				    he = -1.0 * invd * Trans(c);
				    static_cast<ElementByElementMatrix<SCAL>*>(harmonicext)->AddElementMatrix(i,idnums,ednums,he);
				    if (!symmetric){
				      FlatMatrix<SCAL> het (sizeo, sizei, lh);
				      het = -1.0 * b * invd;
				      static_cast<ElementByElementMatrix<SCAL>*>(harmonicexttrans)->AddElementMatrix(i,ednums,idnums,het);
				    }
				    static_cast<ElementByElementMatrix<SCAL>*>(innersolve)->AddElementMatrix(i,idnums,idnums,d);
				  }
				  
				  idc = c * Trans (invd);
				  a -= b * Trans (idc);
				  
#endif
			    
				  if (printelmat) 
				    {
				      testout->precision(8);
				      (*testout) << "Schur elmat = " << endl << a << endl;
				    }
			 
				  if (elmat_ev)
				    {
				      testout->precision(8);
				
				      (*testout) << "EV of Schur complement:" << endl;
#ifdef LAPACK
				      // LapackEigenSystem(a, lh);

				      Matrix<SCAL> hmat(a.Height());
				      Vector<SCAL> lami2(a.Height());
				      hmat = a;
				      LapackEigenValuesSymmetric (hmat, lami2);
				      (*testout) << "lapack ev = " << lami2 << endl;

#else
				      Vector<> lami(a.Height());
				      Matrix<> evecs(a.Height());
				
				      CalcEigenSystem (a, lami, evecs);
				      (*testout) << "lami = " << endl << lami << endl; // << "evecs = " << endl << evecs << endl;
#endif
				
				    }
				  
				  
				  for (int k = 0; k < odofs.Size(); k++)
				    for (int l = 0; l < odofs.Size(); l++)
				      sum_elmat(odofs[k], odofs[l]) = a(k,l);

				  
				  
				  if (linearform && (!keep_internal ))
				    {
				      
				      FlatVector<SCAL> elvec (size, lh);
				      linearform -> GetVector().GetIndirect (dnums, elvec);
				      FlatVector<SCAL> hfi1(sizei, lh);
				      FlatVector<SCAL> hfi2(sizei, lh);
				      FlatVector<SCAL> hfo(sizeo, lh);
				      
				      for (int k = 0; k < idofs.Size(); k++)
					hfi1(k) = elvec(idofs[k]);
				      
#ifdef LAPACK
				      hfo = b * hfi1;
#else
				      hfi2 = d * hfi1;
				      hfo = b * hfi2;
#endif
				      for (int k = 0; k < odofs.Size(); k++)
					elvec(odofs[k]) -= hfo(k);
				      
				      linearform->GetVector().SetIndirect (dnums, elvec);
				      
				    }
				  
				  for (int k = 0; k < idofs1.Size(); k++)
				    dnums[idofs1[k]] = -1;
				}
				
			      NgProfiler::StopTimer (statcondtimer);
			    }

			  if (printelmat)
			    {
#pragma omp critical (printelmatschur)
			      {
				*testout<< "elem " << i << ", elmat = " << endl << sum_elmat << endl;
			      }
			    }
                      

			  if (element_coloring)
			    AddElementMatrix (dnums, dnums, sum_elmat, 1, i, lh);
			  else
#pragma omp critical (addvolelement)
			    {
			      AddElementMatrix (dnums, dnums, sum_elmat, 1, i, lh);
			    }
			  
			  for (int j = 0; j < dnums.Size(); j++)
			    if (dnums[j] != -1)
			      useddof.Set (dnums[j]);

			  NgProfiler::StopTimer (timer3);
			}
		    }//end of parallel
		  }//end loop over colors

                cout << "\rassemble element " << ne << "/" << ne << endl;

		if (linearform && keep_internal)
		  {
		    cout << "\rmodifying condensated rhs";
		    linearform -> GetVector() += GetHarmonicExtensionTrans() * linearform -> GetVector();
		    cout << "\t done" << endl;
		  }
		
              }




            if (hasinner && diagonal)
              {
                ElementTransformation eltrans;
                void * heapp = clh.GetPointer();
                for (int i = 0; i < ne; i++)
                  {
		    gcnt++;
                    if ( ma.IsGhostEl(i) )
                      continue;

                    if (clock()-prevtime > 0.1 * CLOCKS_PER_SEC)
                      {
                        cout << "\rassemble element " << i << "/" << ne << flush;
                        ma.SetThreadPercentage ( 100.0*gcnt / (loopsteps) );
                        prevtime = clock();
                      }
		
                    clh.CleanUp(heapp);
		  
                    if (!fespace.DefinedOn (ma.GetElIndex (i))) continue;
		  
                    const FiniteElement & fel = fespace.GetFE (i, clh);
		  
                    ma.GetElementTransformation (i, eltrans, clh);
                    fespace.GetDofNrs (i, dnums);
		  
		  
                    FlatVector<SCAL> sum_diag(dnums.Size()*fespace.GetDimension(), clh);
                    sum_diag = 0;
                    for (int j = 0; j < NumIntegrators(); j++)
                      {
                        const BilinearFormIntegrator & bfi = *parts[j];
			if (bfi.SkeletonForm()) continue;
		        if (bfi.BoundaryForm()) continue;
                        if (!bfi.DefinedOn (ma.GetElIndex (i))) continue;
		      
                        FlatVector<double> diag;
                        try
                          {
                            clock_t starttime;
                            double time;
                            starttime = clock();
			  
                            bfi.CalcElementMatrixDiag (fel, eltrans, diag, clh);
			  
                            time = double(clock() - starttime) / CLOCKS_PER_SEC;
                            // cout << "time = " << time << endl;
			  
                            if (printelmat) //  || fel.ElementType() == ET_TET)
                              {
                                testout->precision(8);
                                (*testout) << "elnum= " << i << endl;
                                (*testout) << "integrator " << bfi.Name() << endl;
                                (*testout) << "dnums = " << endl << dnums << endl;
                                (*testout) << "diag-elmat = " << endl << diag << endl;
                              }
                          }
                        catch (Exception & e)
                          {
                            e.Append (string("in Assemble Element Matrix, bfi = ") + 
                                      bfi.Name() + string("\n"));
                            throw;
                          }
                        catch (exception & e)
                          {
                            throw (Exception (string(e.what()) +
                                              string("in Assemble Element Matrix, bfi = ") + 
                                              bfi.Name() + string("\n")));
                          }
		      
                        sum_diag += diag;
                      }


                    AddDiagElementMatrix (dnums, sum_diag, 1, i, clh);

                    for (int j = 0; j < dnums.Size(); j++)
                      if (dnums[j] != -1)
                        useddof.Set (dnums[j]);
                  }
                clh.CleanUp(heapp);
                cout << "\rassemble element " << ne << "/" << ne << endl;
              }





            if (hasbound)
              {
                int cnt = 0;
#pragma omp parallel 
                {
                  LocalHeap lh = clh.Split();

                  ElementTransformation eltrans;
                  Array<int> dnums, idofs, idofs1, odofs;
#pragma omp for 
                  for (int i = 0; i < nse; i++)
                    {
                      if ( ma.IsGhostSEl ( i ) ) continue;
		      
#pragma omp critical(printmatasstatus)
		      {
			cnt++;
			gcnt++;
			if (clock()-prevtime > 0.1 * CLOCKS_PER_SEC)
			  {
			    {
			      cout << "\rassemble surface element " << cnt << "/" << nse << flush;
			      ma.SetThreadPercentage ( 100.0*gcnt / (loopsteps) );
			      prevtime = clock();
			    }
			  }
		      }
		      /*
#pragma omp critical(printmatasstatus2)
                      {
                        cnt++;
			gcnt++;
                        if (cnt % 100 == 0)
                          cout << "\rassemble surface element " << cnt << "/" << nse << flush;
                        ma.SetThreadPercentage ( 100.0*(gcnt) / (loopsteps) );
                      }
		      */



                      lh.CleanUp();
		  
                      if (!fespace.DefinedOnBoundary (ma.GetSElIndex (i))) continue;

		      NgProfiler::StartTimer (timerb1);
		      
                      const FiniteElement & fel = fespace.GetSFE (i, lh);
		      
                      ma.GetSurfaceElementTransformation (i, eltrans, lh);
                      fespace.GetSDofNrs (i, dnums);

                      if(fel.GetNDof() != dnums.Size())
                        {
                          cout << "Surface fel:GetNDof() = " << fel.GetNDof() << endl;
                          cout << "dnums.Size() = " << dnums.Size() << endl;

                          (*testout) << "fel:GetNDof() = " << fel.GetNDof() << endl;
                          (*testout) << "dnums.Size() = " << dnums.Size() << endl;
                          (*testout) << "dnums = " << dnums << endl;
                          throw Exception ( "Inconsistent number of degrees of freedom " );
                        }

		      NgProfiler::StopTimer (timerb1);

                      for (int j = 0; j < NumIntegrators(); j++)
                        {
                          const BilinearFormIntegrator & bfi = *parts[j];
		    
                          if (!bfi.BoundaryForm()) continue;
                          if (bfi.SkeletonForm()) continue;
                          if (!bfi.DefinedOn (ma.GetSElIndex (i))) continue;		    

                          for (int k = 0; k < dnums.Size(); k++)
                            if (dnums[k] != -1)
                              useddof.Set (dnums[k]);

			  NgProfiler::StartTimer (timerb2);

                          int elmat_size = dnums.Size()*fespace.GetDimension();
                          FlatMatrix<SCAL> elmat(elmat_size, lh);

			  bfi.CalcElementMatrix (fel, eltrans, elmat, lh);
                          fespace.TransformMat (i, true, elmat, TRANSFORM_MAT_LEFT_RIGHT);

			  NgProfiler::StopTimer (timerb2);


			  if (printelmat)
			    {
			      testout->precision(8);
			      
			      (*testout) << "surface-elnum= " << i << endl;
			      (*testout) << "integrator " << bfi.Name() << endl;
			      (*testout) << "dnums = " << endl << dnums << endl;
			      (*testout) << "element-index = " << eltrans.GetElementIndex() << endl;
			      (*testout) << "elmat = " << endl << elmat << endl;
			    }
			  
			  
			  if (elmat_ev)
			    {
			      testout->precision(8);
			      
			      (*testout) << "elind = " << eltrans.GetElementIndex() << endl;
#ifdef LAPACK
			      LapackEigenSystem(elmat, lh);
#else
			      Vector<> lami(elmat.Height());
			      Matrix<> evecs(elmat.Height());
			      
			      CalcEigenSystem (elmat, lami, evecs);
			      (*testout) << "lami = " << endl << lami << endl;
#endif
			      // << "evecs = " << endl << evecs << endl;
			    }
			  
			  // 			for(int k=0; k<elmat.Height(); k++)
			  // 			  if(fabs(elmat(k,k)) < 1e-7 && dnums[k] != -1)
			  // 			    cout << "dnums " << dnums << " elmat " << elmat << endl; 
			  
			  
			  NgProfiler::StartTimer (timerb3);

#pragma omp critical (addelmatboundary)
			  {
			    AddElementMatrix (dnums, dnums, elmat, 0, i, lh);
			  }
			  NgProfiler::StopTimer (timerb3);
			}
                    }
		}//endof parallel 
		cout << "\rassemble surface element " << cnt << "/" << nse << endl;
	      }//endof hasbound

	    if (hasskeletonbound)
	      {
		int cnt = 0;	      
#pragma omp parallel
		{
		  LocalHeap lh = clh.Split();
		  Array<int> fnums, elnums, vnums, dnums;
		  ElementTransformation eltrans, seltrans;
		
#pragma omp for 
		  for (int i = 0; i < nse; i++)
		    {
		      if ( ma.IsGhostSEl ( i ) ) continue;
#pragma omp critical(printmatasstatus2)
		      {
			cnt++;
			gcnt++;
			if (cnt % 10 == 0)
			  cout << "\rassemble facet surface element " << cnt << "/" << nse << flush;
			ma.SetThreadPercentage ( 100.0*(gcnt) / (loopsteps) );
		      }
		  
		      HeapReset hr(lh);
		      
		      if (!fespace.DefinedOnBoundary (ma.GetSElIndex (i))) continue;
		      ma.GetSElFacets(i,fnums);
		      int fac = fnums[0];
		      ma.GetFacetElements(fac,elnums);
		      int el = elnums[0];
		      ma.GetElFacets(el,fnums);
				  
		      const FiniteElement & fel = fespace.GetFE (el, lh);
		      int facnr = 0;
		      for (int k=0; k<fnums.Size(); k++)
			if(fac==fnums[k]) facnr = k;
		      ma.GetElVertices (el, vnums);	
		      ma.GetElementTransformation (el, eltrans, lh);
		      ma.GetSurfaceElementTransformation (i, seltrans, lh);
		      fespace.GetDofNrs (el, dnums);
		      if(fel.GetNDof() != dnums.Size())
			{
			  cout << "Surface fel:GetNDof() = " << fel.GetNDof() << endl;
			  cout << "dnums.Size() = " << dnums.Size() << endl;

			  (*testout) << "fel:GetNDof() = " << fel.GetNDof() << endl;
			  (*testout) << "dnums.Size() = " << dnums.Size() << endl;
			  (*testout) << "dnums = " << dnums << endl;
			  throw Exception ( "Inconsistent number of degrees of freedom " );
			}

		      for (int j = 0; j < NumIntegrators(); j++)
			{
			  const BilinearFormIntegrator & bfi = *parts[j];
	      
			  if (!bfi.BoundaryForm()) continue;
			  if (!bfi.SkeletonForm()) continue;

			  if (!bfi.DefinedOn (ma.GetSElIndex(i) )) continue;		    

			  for (int k = 0; k < dnums.Size(); k++)
			    if (dnums[k] != -1)
			      useddof.Set (dnums[k]);

			  int elmat_size = dnums.Size()*fespace.GetDimension();
			  FlatMatrix<SCAL> elmat(elmat_size, lh);

			  // original version did not compile on MacOS V
			  const FacetBilinearFormIntegrator & fbfi = 
			    dynamic_cast<const FacetBilinearFormIntegrator&>(bfi);  
			  fbfi.CalcFacetMatrix (fel,facnr,eltrans,vnums, seltrans, elmat, lh);
			  

			  fespace.TransformMat (i, false, elmat, TRANSFORM_MAT_LEFT_RIGHT);
		    
			  if (printelmat)
			    {
			      testout->precision(8);
			
			      (*testout) << "surface-elnum= " << i << endl;
			      (*testout) << "integrator " << bfi.Name() << endl;
			      (*testout) << "dnums = " << endl << dnums << endl;
			      (*testout) << "element-index = " << eltrans.GetElementIndex() << endl;
			      (*testout) << "elmat = " << endl << elmat << endl;
			    }
		    
		    
			  if (elmat_ev)
			    {
			      testout->precision(8);
			
			      (*testout) << "elind = " << eltrans.GetElementIndex() << endl;
#ifdef LAPACK
			      LapackEigenSystem(elmat, lh);
#else
			      Vector<> lami(elmat.Height());
			      Matrix<> evecs(elmat.Height());
			
			      CalcEigenSystem (elmat, lami, evecs);
			      (*testout) << "lami = " << endl << lami << endl;
#endif
			      // << "evecs = " << endl << evecs << endl;
			    }

			  // 			for(int k=0; k<elmat.Height(); k++)
			  // 			  if(fabs(elmat(k,k)) < 1e-7 && dnums[k] != -1)
			  // 			    cout << "dnums " << dnums << " elmat " << elmat << endl; 
#pragma omp critical(addelemfacbnd)
			  {
			    AddElementMatrix (dnums, dnums, elmat, 0, i, lh);
			  }
			}//end for (numintegrators)
		    }//end for nse		    
		}//end of parallel
		cout << "\rassemble facet surface element " << nse << "/" << nse << endl;	  
	      }//end of hasskeletonbound
            if (hasskeletoninner)
	      {
                int cnt = 0;
#pragma omp parallel 
                {
                  LocalHeap lh = clh.Split();

                  ElementTransformation eltrans1, eltrans2;
                  Array<int> dnums, dnums1, dnums2, elnums, fnums, vnums1, vnums2;
#pragma omp for 
                  for (int i = 0; i < nf; i++)
                    {
		      HeapReset hr(lh);
		      
		      int el1 = -1;
		      int el2 = -1;
		      int facnr1 = -1;
		      int facnr2 = -1;

		      ma.GetFacetElements(i,elnums);
		      el1 = elnums[0];

		      if(elnums.Size()<2) continue;
		      el2 = elnums[1];

		      ma.GetElFacets(el1,fnums);
		      for (int k=0; k<fnums.Size(); k++)
			if(i==fnums[k]) facnr1 = k;

		      ma.GetElFacets(el2,fnums);
		      for (int k=0; k<fnums.Size(); k++)
			if(i==fnums[k]) facnr2 = k;
		      
#pragma omp critical(printmatasstatus2)
                      {
                        cnt++;
			gcnt++;
                        if (cnt % 10 == 0)
                          cout << "\rassemble inner facet element " << cnt << "/" << nf << flush;
                        ma.SetThreadPercentage ( 100.0*(gcnt) / (loopsteps) );
                      }
		      
                      
		  
                      const FiniteElement & fel1 = fespace.GetFE (el1, lh);
                      const FiniteElement & fel2 = fespace.GetFE (el2, lh);
                      ma.GetElementTransformation (el1, eltrans1, lh);
		      ma.GetElementTransformation (el2, eltrans2, lh);
                      fespace.GetDofNrs (el1, dnums1);
		      dnums=dnums1;
                      fespace.GetDofNrs (el2, dnums2);
		      for (int d=0; d < dnums2.Size(); d++)
			dnums.Append(dnums2[d]);
		      ma.GetElVertices (el1, vnums1);
		      ma.GetElVertices (el2, vnums2);
                      if(fel1.GetNDof() != dnums1.Size() || ((elnums.Size()>1) && (fel2.GetNDof() != dnums2.Size() )))
                        {
                          cout << "facet, neighbouring fel(1): GetNDof() = " << fel1.GetNDof() << endl;
                          cout << "facet, neighbouring fel(2): GetNDof() = " << fel2.GetNDof() << endl;
                          cout << "facet, neighbouring fel(1): dnums.Size() = " << fel1.GetNDof() << endl;
                          cout << "facet, neighbouring fel(2): dnums.Size() = " << fel2.GetNDof() << endl;
                          throw Exception ( "Inconsistent number of degrees of freedom " );
                        }
		      for (int j = 0; j < NumIntegrators(); j++)
                        {
                          const BilinearFormIntegrator & bfi = *parts[j];
		    
			  if (!bfi.SkeletonForm()) continue;
			  if (bfi.BoundaryForm()) continue;
                          if (!bfi.DefinedOn (ma.GetElIndex (el1))) continue; //TODO: treat as surface element
                          if (!bfi.DefinedOn (ma.GetElIndex (el2))) continue; //TODO    

                          for (int k = 0; k < dnums.Size(); k++)
                            if (dnums[k] != -1)
                              useddof.Set (dnums[k]);

                          int elmat_size = (dnums1.Size()+dnums2.Size())*fespace.GetDimension();
                          FlatMatrix<SCAL> elmat(elmat_size, lh);

			  const FacetBilinearFormIntegrator & fbfi = 
			    dynamic_cast<const FacetBilinearFormIntegrator&>(bfi);
			  fbfi.CalcFacetMatrix (fel1,facnr1,eltrans1,vnums1,
						fel2,facnr2,eltrans2,vnums2, elmat, lh);
			  *testout << "elmat : \n" << elmat << endl;
                          fespace.TransformMat (i, false, elmat, TRANSFORM_MAT_LEFT_RIGHT);

                          if (printelmat)
                            {
                              testout->precision(8);

                              (*testout) << "facet-elnum= " << i << endl;
                              (*testout) << "integrator " << bfi.Name() << endl;
                              (*testout) << "dnums1 = " << endl << dnums1 << endl;
                              (*testout) << "dnums2 = " << endl << dnums2 << endl;
                              (*testout) << "element1-index = " << eltrans1.GetElementIndex() << endl;
                              (*testout) << "element2-index = " << eltrans2.GetElementIndex() << endl;
                              (*testout) << "elmat = " << endl << elmat << endl;
                            }

		    
                          if (elmat_ev)
                            {
                              testout->precision(8);

                              (*testout) << "elind1 = " << eltrans1.GetElementIndex() << endl;
                              (*testout) << "elind2 = " << eltrans2.GetElementIndex() << endl;
#ifdef LAPACK
                              LapackEigenSystem(elmat, lh);
#else
                              Vector<> lami(elmat.Height());
                              Matrix<> evecs(elmat.Height());
			    
                              CalcEigenSystem (elmat, lami, evecs);
                              (*testout) << "lami = " << endl << lami << endl;
#endif
                              // << "evecs = " << endl << evecs << endl;
                            }

                          // 			for(int k=0; k<elmat.Height(); k++)
                          // 			  if(fabs(elmat(k,k)) < 1e-7 && dnums[k] != -1)
                          // 			    cout << "dnums " << dnums << " elmat " << elmat << endl; 

#pragma omp critical(addelemfacin)
			  {
			    AddElementMatrix (dnums, dnums, elmat, 0, i, lh);
			  }
                        }
                    }
                }
                cout << "\rassemble inner facet element " << nf << "/" << nf << endl;	  
              }
	    ma.SetThreadPercentage ( 100.0 );

            if(NumIndependentIntegrators() > 0)
              {
                DoAssembleIndependent(useddof,clh);
              }


            clh.CleanUp();

            bool assembledspecialelements = false;
	    
	    
	    int nspecel = 0;
#pragma omp parallel 
	    {
	      LocalHeap lh = clh.Split();
	      Array<int> dnums;
	      
#pragma omp for 
	      for (int i = 0; i < fespace.specialelements.Size(); i++)
		{
#pragma omp critical(printmatspecel)
		  {
		    gcnt++;
		    nspecel++;
		    if (i % 10 == 0)
		      cout << "\rassemble special element " << nspecel << "/" << fespace.specialelements.Size() << flush;
		    ma.SetThreadPercentage ( 100.0*(gcnt) / (loopsteps) );
		  }
		  
		  const SpecialElement & el = *fespace.specialelements[i];
		
		  el.GetDofNrs (dnums);
		
		  FlatMatrix<SCAL> elmat(dnums.Size(), lh);
		  el.Assemble (elmat, lh);
		  
#pragma omp critical(printmatspecel2)
		  {
		    for (int j = 0; j < dnums.Size(); j++)
		      if (dnums[j] != -1)
			useddof.Set (dnums[j]);
		    
		    AddElementMatrix (dnums, dnums, elmat, 0, i, lh);
		  }
		  
		  assembledspecialelements = true;
		  lh.CleanUp();
		}
	    }
	    if(assembledspecialelements) cout << "\rassemble special element " 
					      << fespace.specialelements.Size() << "/" << fespace.specialelements.Size() << endl;
	    
	  
            // add eps to avoid empty lines
            FlatMatrix<SCAL> elmat (fespace.GetDimension(), clh);
            elmat = 0;
            dnums.SetSize(1);

            void * p = clh.GetPointer();
            if (eps_regularization != 0)
              {
                for (int i = 0; i < elmat.Height(); i++)
                  elmat(i, i) = eps_regularization;
                for (int i = 0; i < ndof; i++)
                  {
                    clh.CleanUp (p);
                    dnums[0] = i; 
                    AddElementMatrix (dnums, dnums, elmat, 0, i, clh);
                  }
              }
            if (unuseddiag != 0)
              {
                for (int i = 0; i < elmat.Height(); i++)
                  elmat(i, i) = unuseddiag;
                for (int i = 0; i < ndof; i++)
                  if (!useddof.Test (i))
                    {
                      //(*testout) << "unused: " << i << endl;
                      clh.CleanUp (p);
                      dnums[0] = i;
                      AddElementMatrix (dnums, dnums, elmat, 0, i, clh);
                    }
              }


            if (print)
              (*testout) << "mat = " << endl << GetMatrix() << endl;

            int cntused = 0;
            for (int i = 0; i < useddof.Size(); i++)
              if (useddof.Test(i))
                cntused++;
	    
            cout << "used " << cntused
                 << ", unused = " << useddof.Size()-cntused
                 << ", total = " << useddof.Size() << endl;

            ma.PopStatus ();
          }

        else
	

          {
            cout << "assemble mixed not implemented" << endl;
#ifdef ABC
            // mixed spaces
      
            Array<int> dnums1;
            Array<int> dnums2;

            //      DenseMatrix elmat;
            ElementTransformation eltrans;
      
            int ne = ma.GetNE();
      
            BaseMatrix & mat = GetMatrix();
            //      (*mat) = 0;
      
            cout << "assemble" << endl;
            // LocalHeap lh (5000000);

            int hasbound = 0;
            int hasinner = 0;

            for (int j = 1; j <= NumIntegrators(); j++)
              {
                const BilinearFormIntegrator & bfi = *GetIntegrator(j);
                if (bfi.BoundaryForm())
                  hasbound = 1;
                else
                  hasinner = 1;
              }

            if (hasinner)
              for (int i = 1; i <= ne; i++)
                {
                  if (i % 100 == 0)
                    cout << "." << flush;
                  clh.CleanUp();
	  
                  const FiniteElement & fel1 = fespace.GetFE (i, lh);
                  const FiniteElement & fel2 = fespace2->GetFE (i, lh);

                  ma.GetElementTransformation (i, eltrans, lh);
                  fespace.GetDofNrs (i, dnums1);
                  fespace2->GetDofNrs (i, dnums2);
	  
                  for (int j = 0; j < NumIntegrators(); j++)
                    {
                      const BilinearFormIntegrator & bfi = *GetIntegrator(j);
                      if (bfi.BoundaryForm()) continue;
	      
                      /*
                      // elmat = 
                      bfi.AssembleMixedElementMatrix (fel1, fel2, eltrans, 
                      lh);
                      */
                      //	      (*testout) << "mixed mat = " << elmat << endl;

                      //	      fespace.TransformMatrix (i, elmat);

                      /*
                        dynamic_cast<SparseSystemMatrixRectangle<SysVector1d, SysVector2d > *> (mat)
                        -> AddMixedElementMatrix (dnums2, dnums1, elmat);
                      */
                    }
                }
      

            cout << " boundary terms ";

            int nse = ma.GetNSE();
            if (hasbound)
              for (int i = 1; i <= nse; i++)
                {
                  const FiniteElement & fel1 = fespace.GetSFE (i, lh);
                  const FiniteElement & fel2 = fespace2->GetSFE (i, lh);
	  
                  ma.GetSurfaceElementTransformation (i, eltrans, lh);
                  fespace.GetSDofNrs (i, dnums1);
                  fespace2->GetSDofNrs (i, dnums2);
	  
                  for (int j = 0; j < NumIntegrators(); j++)
                    {
                      const BilinearFormIntegrator & bfi = *parts[j];

                      if (!bfi.BoundaryForm()) continue;
	      
                      //  elmat = 
                      //	      bfi.AssembleMixedElementMatrix (fel1, fel2, eltrans, lh);
                      /*
                        fespace.Transform (i, true, elmat, TRANSFORM_MAT_RIGHT);
                        fespace2.Transform (i, true, elmat, TRANFORM_MAT_LEFT);
                      */
                      /*
                        dynamic_cast<SparseSystemMatrixRectangle<SysVector1d, SysVector1d> *> (mat)
                        -> AddMixedElementMatrix (dnums2, dnums1, elmat);
                      */
                    }
                }

            // if (print)
            //	(*testout) << "mat = " << (*mat) << endl;

            cout << endl;
#endif
          }
        //  cout << "done" << endl;
        //  WriteMatrix (*testout);
      

#ifdef PARALLEL
        if ( id > 0 )
          {
            ParallelBaseMatrix & pbm = dynamic_cast<ParallelBaseMatrix &>(*mats.Last());
            pbm . CalcConsistentMat(clh);
          }
#endif

      }
    catch (Exception & e)
      {
cout << "catch in AssembleBilinearform 2" << endl;
        e.Append (string ("in Assemble BilinearForm '") + 
                  GetName() + string("'\n"));
        throw;
      }
    catch (exception & e)
      {
        throw (Exception (string(e.what()) +
                          string("\n in Assemble BilinearForm '" + GetName() + "'\n")));
      }
  }


  template <class SCAL>
  void S_BilinearForm<SCAL> :: DoAssembleIndependent (BitArray & useddof, LocalHeap & lh)
  {
  }



  template <class SCAL>
  void S_BilinearForm<SCAL> :: ComputeInternal (BaseVector & u, const BaseVector & f, LocalHeap & clh) const
  {
    if (!eliminate_internal) return;

    /*
    if (!linearform)
      throw Exception ("ComputeInternal needs a linear-form");
    */

    static int timer = NgProfiler::CreateTimer ("Compute Internal");
    NgProfiler::RegionTimer reg (timer);


    try
      {
        ma.PushStatus ("Compute Internal");

        int ne = ma.GetNE();

        bool hasinner = 0;

        for (int j = 0; j < NumIntegrators(); j++)
          {
            if (!parts[j] -> BoundaryForm())
              hasinner = 1;
          }	  
	 
        clock_t prevtime = clock();
        if (hasinner)
          {
	    if (keep_internal){
	      cout << "compute internal element ... ";
	      
/*	      //Set u_inner to zero
	      for (int i = 0; i < ne; i++){
		HeapReset hr(clh);
		Array<int> dnums;
		fespace.GetDofNrs (i, dnums, LOCAL_DOF);		
		FlatVector<SCAL> elu (dnums.Size(), clh);
		elu = 0.0;
		u.SetIndirect (dnums, elu);
	      }
*/

	      if (linearform)
		u += GetInnerSolve() * linearform -> GetVector();
	      else
		u += GetInnerSolve() * f;


	      u += GetHarmonicExtension() * u;
	      cout << "done" << endl;
	    }
	    else
	    {  
	    
	      int cnt = 0;
#pragma omp parallel
	      {
		LocalHeap lh = clh.Split();
		Array<int> dnums, idofs;
		ElementTransformation eltrans;
	      
	      
#pragma omp for
              for (int i = 0; i < ne; i++)
                {
		  
                  // PARALLEL
                  if ( ma.IsGhostEl( i ) ) {continue;}
		
#pragma omp critical(printinternalstatus)
		    {
		      cnt++;
		      if (clock()-prevtime > 0.1 * CLOCKS_PER_SEC)
			{
			  cout << "\rcompute internal element " << cnt << "/" << ne << flush;
			  ma.SetThreadPercentage ( 100.0*cnt / ne );
			  prevtime = clock();
			}
		    }
		
		    HeapReset hr(lh);
		
		    if (!fespace.DefinedOn (ma.GetElIndex (i))) continue;
		    
		    const FiniteElement & fel = fespace.GetFE (i, lh);
		
		    ma.GetElementTransformation (i, eltrans, lh);
		    fespace.GetDofNrs (i, dnums);
		
		    int elmat_size = dnums.Size()*fespace.GetDimension();
		    FlatMatrix<SCAL> sum_elmat(elmat_size, lh);
		    sum_elmat = 0;
		    for (int j = 0; j < NumIntegrators(); j++)
		      {
			HeapReset hr(lh);
			const BilinearFormIntegrator & bfi = *parts[j];
		    
			if (bfi.BoundaryForm()) continue;
			if (!bfi.DefinedOn (ma.GetElIndex (i))) continue;
		    
			FlatMatrix<SCAL> elmat(elmat_size, lh);
			bfi.CalcElementMatrix (fel, eltrans, elmat, lh);
		    
			sum_elmat += elmat;
		      }
		
		    fespace.TransformMat (i, false, sum_elmat, TRANSFORM_MAT_LEFT_RIGHT);
		
		    // fel.GetInternalDofs(idofs);
		    fespace.GetDofNrs (i, idofs, LOCAL_DOF);
		    for (int j = 0; j < idofs.Size(); j++)
		      for (int k = 0; k < dnums.Size(); k++)
			if (dnums[k] == idofs[j])
			  {
			    idofs[j] = k; 
			    break;
			  }




		    // (*testout) << "compute internal element = " << i << endl;
		    //		(*testout) << "mat = " << endl << sum_elmat << endl;

		    if (idofs.Size())
		      {
			int dim = sum_elmat.Height() / dnums.Size();
		    
			FlatVector<SCAL> elf (sum_elmat.Height(), lh);
			FlatVector<SCAL> elu (sum_elmat.Height(), lh);
			FlatVector<SCAL> resi(dim*idofs.Size(), lh);
			FlatVector<SCAL> wi(dim*idofs.Size(), lh);

			FlatMatrix<SCAL> ai(dim*idofs.Size(), lh);
		    
			if (linearform)
			  linearform->GetVector().GetIndirect (dnums, elf);
			else
			  f.GetIndirect (dnums, elf);
			u.GetIndirect (dnums, elu);
		    

			// compute residuum
			elf -= sum_elmat * elu;

			for (int jj = 0; jj < idofs.Size(); jj++)
			  for (int j = 0; j < dim; j++)
			    {
			      for (int kk = 0; kk < idofs.Size(); kk++)
				for (int k = 0; k < dim; k++)
				  {
				    ai(jj*dim+j, kk*dim+k) =
				      sum_elmat(idofs[jj]*dim+j, idofs[kk]*dim+k);
				  }
			      resi(jj*dim+j) = elf(idofs[jj]*dim+j);
			    }
		  

			//		    *testout << "residuum = " << resi << endl;

			// CholeskyFactors<SCAL> inv_ai(ai);
			// inv_ai.Mult (resi, wi);
		    
#ifdef LAPACK
			FlatMatrix<SCAL> mresi(1,resi.Size(), &resi(0));
			LapackAInvBt (ai, mresi, 'T');
			wi = resi;
#else
			FlatMatrix<SCAL> inv_ai(dim*idofs.Size(), lh);
			CalcInverse (ai, inv_ai);
			wi = inv_ai * resi;
#endif

		      
			//		    *testout << "inv_ai = " << endl << inv_ai << endl;
		      
			for (int jj = 0; jj < idofs.Size(); jj++)
			  for (int j = 0; j < dim; j++)
			    elu(idofs[jj]*dim+j) += wi(jj*dim+j);
		    
			//		    *testout << "elu = " << endl << elu << endl;

			u.SetIndirect (dnums, elu);
		      }
		  }//end of sum over elements
	      }//end of parallel
            cout << "\rcompute internal element " << ne << "/" << ne << endl;
	    }//end of keep_internal-if
          }
	  
        ma.SetThreadPercentage ( 100.0 );
      
        ma.PopStatus ();
      }
    catch (Exception & e)
      {
        stringstream ost;
        ost << "in ComputeInternal" << endl;
        e.Append (ost.str());
        throw;
      }
    catch (exception & e)
      {
        throw (Exception (string(e.what()) +
                          string("\n in ComputeInternal\n")));
      }
  }










  template <class SCAL>
  void S_BilinearForm<SCAL> :: AssembleLinearization (const BaseVector & lin,
                                                      LocalHeap & lh, 
                                                      bool reallocate)
  {
    try
      {
        Array<int> dnums;
        ElementTransformation eltrans;
      
        int ndof = fespace.GetNDof();
        BitArray useddof(ndof);
        useddof.Clear();
      
        int ne = ma.GetNE();
      
        BaseMatrix & mat = GetMatrix();
        mat = 0.0;
      
        cout << "Assemble linearization" << endl;
      
      
        bool hasbound = 0;
        bool hasinner = 0;
      
        for (int j = 0; j < NumIntegrators(); j++)
          {
            if (parts[j] -> BoundaryForm())
              hasbound = 1;
            else
              hasinner = 1;
          }
      
        if (hasinner)
          {
            for (int i = 0; i < ne; i++)
              {
                if (i % 10 == 0)
                  cout << "\rassemble element " << i << "/" << ne << flush;
                lh.CleanUp();
	      
                if (!fespace.DefinedOn (ma.GetElIndex (i))) continue;
	      
                const FiniteElement & fel = fespace.GetFE (i, lh);
                ma.GetElementTransformation (i, eltrans, lh);
                fespace.GetDofNrs (i, dnums);
	      
                for (int j = 0; j < dnums.Size(); j++)
                  if (dnums[j] != -1)
                    useddof.Set (dnums[j]);
	      
                FlatMatrix<SCAL> sum_elmat(dnums.Size()*fespace.GetDimension(), lh);
                FlatMatrix<SCAL> elmat(dnums.Size()*fespace.GetDimension(), lh);
                sum_elmat = 0;

                FlatVector<SCAL> elveclin (dnums.Size()*fespace.GetDimension(), lh);
                lin.GetIndirect (dnums, elveclin);
                fespace.TransformVec (i, false, elveclin, TRANSFORM_SOL);

                for (int j = 0; j < NumIntegrators(); j++)
                  {
                    const BilinearFormIntegrator & bfi = *parts[j];
		  
                    if (bfi.BoundaryForm()) continue;
                    if (!bfi.DefinedOn (ma.GetElIndex (i))) continue;
		  
                    try
                      {
                        bfi.CalcLinearizedElementMatrix (fel, eltrans, elveclin, elmat, lh);

                        if (printelmat) 
                          {
                            testout->precision(8);
                            (*testout) << "elnum= " << i << endl;
                            (*testout) << "eltype " << fel.ElementType() << endl;
                            (*testout) << "integrator " << bfi.Name() << endl;
                            (*testout) << "dnums = " << endl << dnums << endl;
                            (*testout) << "elveclin = " << endl << elveclin << endl;
                            (*testout) << "elmat = " << endl << elmat << endl;
                          }




                        //////////////////////////////////////////////////
                        /*		      
                        //		      cout << " material: " << ma.GetElMaterial(i) << endl;
		      			
                        cout << " assemble linearization, elmat: " << endl;
                        cout << elmat << endl;

                        FlatMatrix<SCAL> new_elmat(dnums.Size()*fespace.GetDimension(), lh);
                        FlatVector<SCAL> e(dnums.Size()*fespace.GetDimension(), lh);
                        FlatVector<SCAL> temp(dnums.Size()*fespace.GetDimension(), lh);
                        FlatVector<SCAL> y1(dnums.Size()*fespace.GetDimension(), lh);
                        FlatVector<SCAL> y2(dnums.Size()*fespace.GetDimension(), lh);

                        double eps = 1e-5;
                        for ( int ii=0; ii<e.Size(); ii++ )
                        {
                        e = 0;
                        e(ii) = 1;
                        temp = elveclin + eps*e;
                        bfi.ApplyElementMatrix(fel,eltrans,temp,y1,0, lh);
                        bfi.ApplyElementMatrix(fel,eltrans,elveclin,y2, 0, lh);
                        temp = y1-y2;
                        for ( int jj=0; jj<new_elmat.Width(); jj++ ) new_elmat(jj,ii) = temp(jj)/eps;
                        }

                        cout << " elmat by num. diff:" << endl << new_elmat << endl;
                        */
                        //////////////////////////////////////////////////

                      }
                    catch (Exception & e)
                      {
                        e.Append (string("in Assemble Element Mat, bfi = ") + 
                                  bfi.Name() + string("\n"));
                        throw;
                      }
                    catch (exception & e)
                      {
                        throw (Exception (string(e.what()) +
                                          string("in Assemble Element Mat, bfi = ") + 
                                          bfi.Name() + string("\n")));
                      }
		  
                    sum_elmat += elmat;
                  }
	      
                fespace.TransformMat (i, false, sum_elmat, TRANSFORM_MAT_LEFT_RIGHT);
                AddElementMatrix (dnums, dnums, sum_elmat, 1, i, lh);
              }
            cout << "\rassemble element " << ne << "/" << ne << endl;
          }
      
        // (*testout) << "Assemble Mat, vol, mat = " << endl << GetMatrix() << endl;
      
        int nse = ma.GetNSE();
        if (hasbound)
          {
            for (int i = 0; i < nse; i++)
              {
                if (i % 100 == 0)
                  cout << "\rassemble surface element " << i << "/" << nse << flush;
                lh.CleanUp();
	      
                if (!fespace.DefinedOnBoundary (ma.GetSElIndex (i))) continue;
	      
                const FiniteElement & fel = fespace.GetSFE (i, lh);
	      
                ma.GetSurfaceElementTransformation (i, eltrans, lh);
                fespace.GetSDofNrs (i, dnums);
	      
                for (int j = 0; j < dnums.Size(); j++)
                  if (dnums[j] != -1)
                    useddof.Set (dnums[j]);

                FlatVector<SCAL> elveclin (dnums.Size()*fespace.GetDimension(), lh);
                FlatMatrix<SCAL> elmat (dnums.Size()*fespace.GetDimension(), lh);

                lin.GetIndirect (dnums, elveclin);
                fespace.TransformVec (i, true, elveclin, TRANSFORM_SOL);
	      
                for (int j = 0; j < NumIntegrators(); j++)
                  {
                    const BilinearFormIntegrator & bfi = *parts[j];
		  
                    if (!bfi.BoundaryForm()) continue;

		  
                    bfi.CalcLinearizedElementMatrix (fel, eltrans, elveclin, elmat, lh);
		  	  
                    fespace.TransformMat (i, true, elmat, TRANSFORM_MAT_LEFT_RIGHT);
                    AddElementMatrix (dnums, dnums, elmat, 0, i, lh);


                    if (printelmat) 
                      {
                        testout->precision(8);
                        (*testout) << "surface-elnum= " << i << endl;
                        (*testout) << "eltype " << fel.ElementType() << endl;
                        (*testout) << "integrator " << bfi.Name() << endl;
                        (*testout) << "dnums = " << endl << dnums << endl;
                        (*testout) << "elveclin = " << endl << elveclin << endl;
                        (*testout) << "elmat = " << endl << elmat << endl;
                      }



                  }
              }
            cout << "\rassemble surface element " << nse << "/" << nse << endl;	  
          }
      
      
        if (fespace.specialelements.Size())
          cout << "special elements: " << fespace.specialelements.Size() << endl;
        lh.CleanUp();
        for (int i = 0; i < fespace.specialelements.Size(); i++)
          {
	  
            const SpecialElement & el = *fespace.specialelements[i];
            el.GetDofNrs (dnums);
	  
            for (int j = 0; j < dnums.Size(); j++)
              if (dnums[j] != -1)
                useddof.Set (dnums[j]);
	  
            FlatMatrix<SCAL> elmat;
            el.Assemble (elmat, lh);
	  
            AddElementMatrix (dnums, dnums, elmat, 0, i, lh);
            lh.CleanUp();
          }
      
      
        //	  fespace.LockSomeDofs (GetMatrix());
      
      
        // add eps to avoid empty lines
        FlatMatrix<SCAL> elmat (fespace.GetDimension(), lh);
        elmat = 0;
        dnums.SetSize(1);
      
        void * p = lh.GetPointer();
        if (eps_regularization != 0)
          {
            for (int i = 0; i < elmat.Height(); i++)
              elmat(i, i) = eps_regularization;
            for (int i = 0; i < ndof; i++)
              {
                lh.CleanUp (p);
                dnums[0] = i; 
                AddElementMatrix (dnums, dnums, elmat, 0, i, lh);
              }
          }
        if (unuseddiag != 0)
          {
            for (int i = 0; i < elmat.Height(); i++)
              elmat(i, i) = unuseddiag;
            for (int i = 0; i < ndof; i++)
              if (!useddof.Test (i))
                {
                  // (*testout) << "unused: " << i << endl;
                  lh.CleanUp (p);
                  dnums[0] = i;
                  AddElementMatrix (dnums, dnums, elmat, 0, i, lh);
                }
          }
      
        //	  (*testout) << "mat = " << endl << GetMatrix() << endl;
        /*
          if (mat.Height() < 100)
          mat.Print (cout);
        */
        int cntused = 0;
        for (int i = 0; i < useddof.Size(); i++)
          if (useddof.Test(i))
            cntused++;
        cout << "used " << cntused
             << ", unused = " << useddof.Size()-cntused
             << ", total = " << useddof.Size() << endl;
  
      }
    catch (Exception & e)
      {
        stringstream ost;
        ost << "in Assemble BilinearForm" << endl;
        e.Append (ost.str());
        throw;
      }
    catch (exception & e)
      {
        throw (Exception (string(e.what()) +
                          string("\n in Assemble BilinearForm\n")));
      }
  }









  template <class SCAL>
  void S_BilinearForm<SCAL> :: AddMatrix1 (SCAL val,
                                           const BaseVector & x,
                                           BaseVector & y) const
  {
    static int timer = NgProfiler::CreateTimer ("Apply Matrix");
    NgProfiler::RegionTimer reg (timer);

    static int lh_size = 20000000;

    if (!MixedSpaces())

      {
        int ne = ma.GetNE();
      
	bool hasbound = false;
	bool hasinner = false;
	bool hasskeletonbound = false;
	bool hasskeletoninner = false;

	for (int j = 0; j < NumIntegrators(); j++)
	  {
            const BilinearFormIntegrator & bfi = *GetIntegrator(j);
	    if (bfi.BoundaryForm())
	      if (bfi.SkeletonForm())
		hasskeletonbound = true;
	      else
		hasbound = true;
	    else
	      if (bfi.SkeletonForm())
		hasskeletoninner = true;
	      else
		hasinner = true;
	  }

        bool done = false;
        int atempt = 0;

        while(!done && atempt < 3)
          {
            try
              {

                int cnt = 0;

                if (hasinner)
#pragma omp parallel
                  {
                    LocalHeap lh(lh_size, "biform-AddMatrix (a)");
                    Array<int> dnums;
                    ElementTransformation eltrans;
                    
#pragma omp for
                    for (int i = 0; i < ne; i++)
                      {
                        lh.CleanUp();
                        
                        if ( ma.IsGhostEl( i ) ) {continue;}
                        
                        if (!fespace.DefinedOn (ma.GetElIndex (i))) continue;
                        
                        const FiniteElement & fel = fespace.GetFE (i, lh);
                        ma.GetElementTransformation (i, eltrans, lh);
                        fespace.GetDofNrs (i, dnums);
                        
                        ApplyElementMatrix(x,y,val,dnums,eltrans,i,0,cnt,lh,&fel);
                      }
                  }

                LocalHeap lh (lh_size, "biform-AddMatrix (b)");
                Array<int> dnums;
                ElementTransformation eltrans;
      
			
                int nse = ma.GetNSE();
                if (hasbound)
                  for (int i = 0; i < nse; i++)
                    {
                      lh.CleanUp();

                      if ( ma.IsGhostSEl( i ) ) {continue;}

                      const FiniteElement & fel = fespace.GetSFE (i, lh);
		      
                      ma.GetSurfaceElementTransformation (i, eltrans, lh);
                      fespace.GetSDofNrs (i, dnums);
		      
                      ApplyElementMatrix(x,y,val,dnums,eltrans,i,1,cnt,lh,&fel);
                    }
			
		if (hasskeletonbound||hasskeletoninner)
		  throw Exception ("No BilinearFormApplication-Implementation for Facet-Integrators yet");
		  
                lh.CleanUp();
                for (int i = 0; i < fespace.specialelements.Size(); i++)
                  {
                    const SpecialElement & el = *fespace.specialelements[i];
                    el.GetDofNrs (dnums);
		    
                    ApplyElementMatrix(x,y,val,dnums,eltrans,i,2,cnt,lh,NULL,&el);
                    lh.CleanUp();
                  }
                done = true;
              }
            catch (LocalHeapOverflow lhex)
              {
                lh_size *= 2;
                atempt++;
                cerr << "Trying automatic heapsize increase to " << lh_size << endl;
              }
          }
      }
    else
      { 
        cout << "apply not implemented for mixed" << endl;
      }
  }






  template <class SCAL>
  void S_BilinearForm<SCAL> :: ApplyLinearizedMatrixAdd1 (SCAL val,
                                                          const BaseVector & lin,
                                                          const BaseVector & x,
                                                          BaseVector & y) const
  {
    if (!MixedSpaces())

      {
        /*
          (*testout) << "val = " << val << endl;
          (*testout) << "applylin, lin = " << endl << lin << endl;
          (*testout) << "global x = " << endl << x << endl;
          (*testout) << "global y,in = " << endl << y << endl;
        */
        Array<int> dnums;
        ElementTransformation eltrans;
      
        int ne = ma.GetNE();
        int dim = GetFESpace().GetDimension(); 
        LocalHeap lh (2000000, "biform-ApplyLinearized");

        int hasbound = 0;
        int hasinner = 0;

        for (int j = 0; j < NumIntegrators(); j++)
          {
            const BilinearFormIntegrator & bfi = *GetIntegrator(j);
            if (bfi.BoundaryForm())
              hasbound = 1;
            else
              hasinner = 1;
          }


        if (hasinner)
          for (int i = 0; i < ne; i++)
            {
              lh.CleanUp();
	  
              const FiniteElement & fel = fespace.GetFE (i, lh);
              ma.GetElementTransformation (i, eltrans, lh);
              fespace.GetDofNrs (i, dnums);
	  
              FlatVector<SCAL> elveclin (dnums.Size() * dim, lh);
              FlatVector<SCAL> elvecx (dnums.Size() * dim, lh);
              FlatVector<SCAL> elvecy (dnums.Size() * dim, lh);

              lin.GetIndirect (dnums, elveclin);
              fespace.TransformVec (i, false, elveclin, TRANSFORM_SOL);

              x.GetIndirect (dnums, elvecx);
              fespace.TransformVec (i, false, elvecx, TRANSFORM_SOL);

              for (int j = 0; j < NumIntegrators(); j++)
                {
                  const BilinearFormIntegrator & bfi = *parts[j];

                  if (bfi.BoundaryForm()) continue;
                  if (!bfi.DefinedOn (ma.GetElIndex (i))) continue;


                  bfi.ApplyLinearizedElementMatrix (fel, eltrans, elveclin, elvecx, elvecy, lh);

                  fespace.TransformVec (i, false, elvecy, TRANSFORM_RHS);

                  elvecy *= val;

                  y.AddIndirect (dnums, elvecy);
                }
            }

        int nse = ma.GetNSE();
        if (hasbound)
          for (int i = 0; i < nse; i++)
            {
              lh.CleanUp();
              const FiniteElement & fel = fespace.GetSFE (i, lh);
	    
              ma.GetSurfaceElementTransformation (i, eltrans, lh);
              fespace.GetSDofNrs (i, dnums);
	    
              FlatVector<SCAL> elveclin (dnums.Size() * dim, lh);
              FlatVector<SCAL> elvecx (dnums.Size() * dim, lh);
              FlatVector<SCAL> elvecy (dnums.Size() * dim, lh);
	    
              lin.GetIndirect (dnums, elveclin);
              fespace.TransformVec (i, true, elveclin, TRANSFORM_SOL);
              x.GetIndirect (dnums, elvecx);
              fespace.TransformVec (i, true, elvecx, TRANSFORM_SOL);
	  
              for (int j = 0; j < NumIntegrators(); j++)
                {
                  const BilinearFormIntegrator & bfi = *parts[j];
		
                  if (!bfi.BoundaryForm()) continue;
                  if (!bfi.DefinedOn (eltrans.GetElementIndex())) continue;
	      
                  bfi.ApplyLinearizedElementMatrix (fel, eltrans, elveclin, elvecx, elvecy, lh);
                  fespace.TransformVec (i, true, elvecy, TRANSFORM_RHS);
                  elvecy *= val;
                  y.AddIndirect (dnums, elvecy);
                }
            }

        lh.CleanUp();
        for (int i = 0; i < fespace.specialelements.Size(); i++)
          {
            const SpecialElement & el = *fespace.specialelements[i];
            el.GetDofNrs (dnums);
	  
            FlatVector<SCAL> elvecx (dnums.Size() * dim, lh);
            FlatVector<SCAL> elvecy (dnums.Size() * dim, lh);

            x.GetIndirect (dnums, elvecx);
            el.Apply (elvecx, elvecy, lh);

            elvecy *= val;
            y.AddIndirect (dnums, elvecy);
            lh.CleanUp();
          }
      }
    else
      { 
        cout << "apply not implemented for mixed" << endl;
      }
  }







  template <class SCAL>
  double S_BilinearForm<SCAL> :: Energy (const BaseVector & x) const
  {
    double energy = 0;

    if (!MixedSpaces())
      {
        Array<int> dnums;
        ElementTransformation eltrans;

        int ne = ma.GetNE();
      
        LocalHeap lh (2000000, "biform-energy");

        int hasbound = 0;
        int hasinner = 0;

        for (int j = 0; j < NumIntegrators(); j++)
          {
            const BilinearFormIntegrator & bfi = *GetIntegrator(j);
            if (bfi.BoundaryForm())
              hasbound = 1;
            else
              hasinner = 1;
          }

        if (hasinner)
          for (int i = 0; i < ne; i++)
            {
              lh.CleanUp();
	  
              const FiniteElement & fel = fespace.GetFE (i, lh);
              ma.GetElementTransformation (i, eltrans, lh);
              fespace.GetDofNrs (i, dnums);
	  
              FlatVector<SCAL> elvecx (dnums.Size() * GetFESpace().GetDimension(), 
                                       lh);

              x.GetIndirect (dnums, elvecx);
              fespace.TransformVec (i, false, elvecx, TRANSFORM_SOL);

              for (int j = 0; j < NumIntegrators(); j++)
                {
                  const BilinearFormIntegrator & bfi = *parts[j];

                  if (bfi.BoundaryForm()) continue;
                  energy += bfi.Energy (fel, eltrans, elvecx, lh);
                }
            }

        // cout << "energy witholut boundary elements " << energy << endl;
        int nse = ma.GetNSE();
        if (hasbound)
          for (int i = 0; i < nse; i++)
            {
              lh.CleanUp();
              const FiniteElement & fel = fespace.GetSFE (i, lh);
	    
              ma.GetSurfaceElementTransformation (i, eltrans, lh);
              fespace.GetSDofNrs (i, dnums);
	    
              FlatVector<SCAL> elvecx (dnums.Size() * GetFESpace().GetDimension(), lh);
              x.GetIndirect (dnums, elvecx);
              fespace.TransformVec (i, true, elvecx, TRANSFORM_SOL);
	  
              for (int j = 0; j < NumIntegrators(); j++)
                {
                  const BilinearFormIntegrator & bfi = *parts[j];
		
                  if (!bfi.BoundaryForm()) continue;
                  energy += bfi.Energy (fel, eltrans, elvecx, lh);
                }
            }
        // cout << "energy with boundary elements " << energy << endl;

        lh.CleanUp();
        for (int i = 0; i < fespace.specialelements.Size(); i++)
          {

            const SpecialElement & el = *fespace.specialelements[i];
            el.GetDofNrs (dnums);

            FlatVector<SCAL> elvecx (dnums.Size() * GetFESpace().GetDimension(), lh);
            x.GetIndirect (dnums, elvecx);
	  
            energy += el.Energy (elvecx, lh);
          }
        lh.CleanUp();
      }
    return energy;
  }


  template <class SCAL>
  void S_BilinearForm<SCAL> :: 
  AddDiagElementMatrix (const Array<int> & dnums1,
                        const FlatVector<SCAL> & diag,
                        bool inner_element, int elnr,
                        LocalHeap & lh)
  {
    throw Exception ("Baseclass::AddDiagElementMatrix");
  }
 



  template class S_BilinearForm<double>;
  template class S_BilinearForm<Complex>;

  template <class TM, class TV>
  T_BilinearForm<TM,TV>::
  T_BilinearForm (const FESpace & afespace, const string & aname, const Flags & flags)
    : S_BilinearForm<TSCAL> (afespace, aname, flags)
  { 
    if (&this->fespace.LowOrderFESpace())
      this->low_order_bilinear_form = 
        new T_BilinearForm<TM,TV> (this->fespace.LowOrderFESpace(),
                                   aname+string(" low-order"), flags);
  }

  template <class TM, class TV>
  T_BilinearForm<TM,TV>::
  T_BilinearForm (const FESpace & afespace, 
                  const FESpace & afespace2,
                  const string & aname,
                  const Flags & flags)
    : S_BilinearForm<TSCAL> (afespace, afespace2, aname, flags) 
  {
    ;
  }

  template <class TM, class TV>
  T_BilinearForm<TM,TV>::
  ~T_BilinearForm ()
  {
    for (int i = 0; i < this->mats.Size(); i++)
      {
        delete this->mats[i];
        this->mats[i] = NULL;
      }
  }



  template <class TM, class TV>
  void T_BilinearForm<TM,TV>::
  AllocateMatrix ()
  {
    if (this->mats.Size() == this->ma.GetNLevels())
      return;

    MatrixGraph * graph = GetGraph (this->ma.GetNLevels()-1, false);

#ifdef PARALLEL
    if ( ntasks > 1 )
      {
        this->mats.Append ( new ParallelSparseMatrix<TM,TV,TV> (*graph,1,
                                                                &this->GetFESpace().GetParallelDofs()) );
      }
    else
#endif
      this->mats.Append (new SparseMatrix<TM,TV,TV> (*graph, 1));
    delete graph;

    if (!this->multilevel || this->low_order_bilinear_form)
      for (int i = 0; i < this->mats.Size()-1; i++)
        {
          delete this->mats[i];
          this->mats[i] = 0;
        }
  }

#ifdef PARALLEL
  template <class TM, class TV>
  void T_BilinearForm<TM,TV>::
  AllocateConsistentMatrix ()
  {
    
    if ( id > 0 )
      {
        MatrixGraph * consistentgraph = 
          // const_cast<FESpace&>(this->fespace).
	  GetConsistentGraph(this->ma.GetNLevels()-1, false);
        ParallelBaseMatrix& pbm = dynamic_cast<ParallelBaseMatrix&>(*this->mats.Last());
        pbm . AllocateConsistentMat(*consistentgraph);
        delete consistentgraph;
      }  
    /*
      if ( id > 0 )
      {
      ParallelBaseMatrix& pbm = dynamic_cast<ParallelBaseMatrix&>(*this->mats.Last());
      pbm . AllocateConsistentMat();
      }
    */
  }
#endif

  template <class TM, class TV>
  void T_BilinearForm<TM,TV>::
  CleanUpLevel ()
  {
    if (!this->multilevel || this->low_order_bilinear_form)
      for (int i = 0; i < this->mats.Size(); i++)
        {
          delete this->mats[i];
          this->mats[i] = 0;
        }
  }



#ifndef PARALLEL
  template <class TM, class TV>
  BaseVector * T_BilinearForm<TM, TV>::
  CreateVector() const
  {
    return new VVector<TV_COL> (this->fespace.GetNDof());
  }
#else 
  template <class TM, class TV>
  BaseVector * T_BilinearForm<TM, TV>::
  CreateVector() const
  {
    const FESpace & afespace = this->fespace;
    if ( &afespace.GetParallelDofs() == 0 )
      return new VVector<TV> (afespace.GetNDof());
    else
      return new ParallelVVector<TV> ( afespace.GetNDof(),& afespace.GetParallelDofs());
  }
#endif

  template <class TM, class TS>
  inline void AddPartOfElementMatrix(TM & dest, const FlatMatrix<TS> & source,
                                     const int start1, const int start2)
  {
    int hi = Height(dest);
    int wi = Width(dest);
	  
    for (int k = 0; k < hi; k++)
      for (int l = 0; l < wi; l++)
        dest(k,l) += source(start1*hi+k, start2*wi+l);
  }
  
  template <>
  inline void AddPartOfElementMatrix(double & dest, 
                                     const FlatMatrix<double> & source,
                                     const int start1, const int start2)
  {
    dest += source(start1, start2);
  }

  template <>
  inline void AddPartOfElementMatrix(Complex & dest, 
                                     const FlatMatrix<Complex> & source,
                                     const int start1, const int start2)
  {
    dest += source(start1, start2);
  }
    
  
  ///
  template <class TM, class TV>
  void T_BilinearForm<TM,TV>::
  AddElementMatrix (const Array<int> & dnums1,
                    const Array<int> & dnums2,
                    const FlatMatrix<TSCAL> & elmat,
                    bool inner_element, int elnr,
                    LocalHeap & lh) 
  {
    // #pragma omp critical (addelmat)
    {
      TMATRIX & mat = dynamic_cast<TMATRIX&> (*this->mats.Last());
      //FlatArray<int> colpos(dnums2.Size(), lh);

      mat.AddElementMatrix (dnums1, dnums2, elmat);

#ifdef OLD
      for (int i = 0; i < dnums1.Size(); i++)
        for (int j = 0; j < dnums2.Size(); j++)
          if (dnums1[i] != -1 && dnums2[j] != -1)
            {
              AddPartOfElementMatrix(mat(dnums1[i], dnums2[j]),
                                     elmat, i, j);
              /*
                TM & mij = mat(dnums1[i], dnums2[j]);
		
                int hi = Height(mij);
                int wi = Width(mij);
		
                for (int k = 0; k < hi; k++)
                for (int l = 0; l < wi; l++)
                mij(k,l) += elmat(i*hi+k, j*wi+l);
              */
            }
#endif
    }
  }


  ///
  template <> void T_BilinearForm<double, double>::
  AddElementMatrix (const Array<int> & dnums1,
                    const Array<int> & dnums2,
                    const FlatMatrix<double> & elmat,
                    bool inner_element, int elnr,
                    LocalHeap & lh) 
  {
    TMATRIX & mat = dynamic_cast<TMATRIX&>(*this->mats.Last());
    
    // pragma omp critical (addelmat)
    {
      mat.AddElementMatrix (dnums1, dnums2, elmat);

      /*
	for (int i = 0; i < dnums1.Size(); i++)
        for (int j = 0; j < dnums2.Size(); j++)
	if (dnums1[i] != -1 && dnums2[j] != -1)
	AddPartOfElementMatrix(mat(dnums1[i], dnums2[j]),
	elmat, i, j);
      */
      //mat(dnums1[i], dnums2[j]) += elmat(i, j);
    }
  }


  template <> void T_BilinearForm<Complex, Complex>::
  AddElementMatrix (const Array<int> & dnums1,
                    const Array<int> & dnums2,
                    const FlatMatrix<Complex> & elmat,
                    bool inner_element, int elnr,
                    LocalHeap & lh)
  {
    TMATRIX & mat = dynamic_cast<TMATRIX&> (*this->mats.Last());

    // #pragma omp critical (addelmat)
    {
      for (int i = 0; i < dnums1.Size(); i++)
        for (int j = 0; j < dnums2.Size(); j++)
          if (dnums1[i] != -1 && dnums2[j] != -1)
            AddPartOfElementMatrix(mat(dnums1[i], dnums2[j]),
                                   elmat, i, j);
      //mat(dnums1[i], dnums2[j]) += elmat(i, j);
    }

  }



  template <> void T_BilinearForm<double, Complex>::
  AddElementMatrix (const Array<int> & dnums1,
                    const Array<int> & dnums2,
                    const FlatMatrix<double> & elmat,
                    bool inner_element, int elnr,
                    LocalHeap & lh)
  {
    TMATRIX & mat = dynamic_cast<TMATRIX&> (*this->mats.Last());

    // #pragma omp critical (addelmat)
    {
      for (int i = 0; i < dnums1.Size(); i++)
        for (int j = 0; j < dnums2.Size(); j++)
          if (dnums1[i] != -1 && dnums2[j] != -1)
            AddPartOfElementMatrix(mat(dnums1[i], dnums2[j]),
                                   elmat, i, j);
      //mat(dnums1[i], dnums2[j]) += elmat(i, j);
    }

  }


  template <class TM, class TV>
  void T_BilinearForm<TM,TV>::LapackEigenSystem(FlatMatrix<TSCAL> & elmat, LocalHeap & lh) const 
  {
    Vector<Complex> lami(elmat.Height());
    Matrix<TSCAL> evecs(elmat.Height());
    FlatMatrix<TSCAL> elmat_save(elmat.Height(), elmat.Width(), lh);
    elmat_save = elmat;
#ifdef LAPACK
    LapackEigenValues (elmat_save, lami, evecs);
    (*testout) << "lami = " 
               << endl << lami << endl << "evecs: " << endl << evecs << endl;
#endif
  }

  template <class TM, class TV>
  void T_BilinearForm<TM,TV>::ApplyElementMatrix(const BaseVector & x,
                                                 BaseVector & y,
                                                 const TSCAL & val,
                                                 const Array<int> & dnums,
                                                 const ElementTransformation & eltrans,
                                                 const int elnum,
                                                 const int type,
                                                 int & cnt,
                                                 LocalHeap & lh,
                                                 const FiniteElement * fel,
                                                 const SpecialElement * sel) const
  {
    FlatVector<TV> elvecx(dnums.Size() * this->GetFESpace().GetDimension(), lh);
    FlatVector<TV> elvecy(dnums.Size() * this->GetFESpace().GetDimension(), lh);

    x.GetIndirect (dnums, elvecx);

    if(type == 0 || type == 1)
      {
        this->fespace.TransformVec (elnum, (type == 1), elvecx, TRANSFORM_SOL);

        for (int j = 0; j < this->NumIntegrators(); j++)
          {
            BilinearFormIntegrator & bfi = *this->parts[j];
	    
            if (bfi.SkeletonForm()) continue;
            if (type == 0 && bfi.BoundaryForm()) continue;
            if (type == 0 && !bfi.DefinedOn (this->ma.GetElIndex (elnum))) continue;
            if (type == 1 && !bfi.BoundaryForm()) continue;
	    
	    
            static int elementtimer = NgProfiler::CreateTimer ("Element matrix application");
            NgProfiler::StartTimer (elementtimer);
	    
	    
            if (this->precompute)
              // bfi.ApplyElementMatrix (*fel, eltrans, elvecx, elvecy, this->precomputed_data[cnt++], lh);
              bfi.ApplyElementMatrix (*fel, eltrans, elvecx, elvecy, 
                                      this->precomputed_data[elnum*this->NumIntegrators()+j], lh);
            else
              bfi.ApplyElementMatrix (*fel, eltrans, elvecx, elvecy, 0, lh);
	    
            NgProfiler::StopTimer (elementtimer);
	    
            /*
              testout->precision (12);
              (*testout) << "el " << i << ", dom = " << ma.GetElIndex(i) << ",integrator = " << typeid(bfi).name() << endl
              << "elx = " << elvecx 
              << "ely = " << elvecy << endl;
            */
            BilinearForm::GetFESpace().TransformVec (elnum, (type == 1), elvecy, TRANSFORM_RHS);
	
            elvecy *= val;
	    // #pragma omp critical(addapply)
            {
              y.AddIndirect (dnums, elvecy);
            }
          }
      }
    else if (type == 2)
      {
        sel->Apply (elvecx, elvecy, lh);
        elvecy *= val;
        y.AddIndirect (dnums, elvecy);
      }
		      
  }


  template <class TM, class TV>
  T_BilinearFormSymmetric<TM,TV> :: 
  T_BilinearFormSymmetric (const FESpace & afespace, const string & aname,
                           const Flags & flags)
    : S_BilinearForm<TSCAL> (afespace, aname, flags) 
  {
    if (&this->fespace.LowOrderFESpace())
      {
        this->low_order_bilinear_form = 
          new T_BilinearFormSymmetric<TM,TV> (this->fespace.LowOrderFESpace(),
                                              aname+string(" low-order"), flags);
      }
  }

  template <class TM, class TV>
  T_BilinearFormSymmetric<TM,TV> :: 
  ~T_BilinearFormSymmetric ()
  {
    for (int i = 0; i < this->mats.Size(); i++)
      delete this->mats[i];
  }

  ///
  template <class TM, class TV>
  void T_BilinearFormSymmetric<TM,TV> :: 
  AllocateMatrix ()
  {
    if (this->mats.Size() == this->ma.GetNLevels())
      return;

    MatrixGraph * graph = GetGraph (this->ma.GetNLevels()-1, true);


#ifdef PARALLEL
    if ( ntasks > 1 )
      this->mats.Append ( new ParallelSparseMatrixSymmetric<TM,TV> 
                          (*graph, 1,&this->GetFESpace().GetParallelDofs() ) ) ;
    else
#endif
      this->mats.Append (new SparseMatrixSymmetric<TM,TV> 
                         (*graph, 1) );

    delete graph;

    if (!this->multilevel || this->low_order_bilinear_form)
      for (int i = 0; i < this->mats.Size()-1; i++)
        {
          delete this->mats[i];
          this->mats[i] = 0;
        }
  }



#ifdef PARALLEL
  template <class TM, class TV>
  void T_BilinearFormSymmetric<TM,TV>::
  AllocateConsistentMatrix ()
  {
    if ( id > 0 )
      {
        MatrixGraph * consistentgraph = 
          // const_cast<FESpace&>(this->fespace).
	  GetConsistentGraph(this->ma.GetNLevels()-1, true);
        ParallelBaseMatrix& pbm = dynamic_cast<ParallelBaseMatrix&>(*this->mats.Last());
        pbm . AllocateConsistentMat(*consistentgraph);
        delete consistentgraph;
      }     
    /*
      if ( id > 0 )
      {
      ParallelBaseMatrix& pbm = dynamic_cast<ParallelBaseMatrix&>(*this->mats.Last());
      pbm . AllocateConsistentMat();
      }
    */
    
	
  }
#endif

  template <class TM, class TV>
  void T_BilinearFormSymmetric<TM,TV>::
  CleanUpLevel ()
  {
    if (!this->multilevel || this->low_order_bilinear_form)
      for (int i = 0; i < this->mats.Size(); i++)
        {
          delete this->mats[i];
          this->mats[i] = 0;
        }
  }


#ifndef PARALLEL
  template <class TM, class TV>
  BaseVector * T_BilinearFormSymmetric<TM,TV> :: 
  CreateVector() const
  {
    return new VVector<TV> (this->fespace.GetNDof());
  }
#else
  template <class TM, class TV>
  BaseVector * T_BilinearFormSymmetric<TM, TV>::
  CreateVector() const
  {
    const FESpace & afespace = this->fespace;
    if ( &afespace.GetParallelDofs() == 0 )
      return new VVector<TV> (afespace.GetNDof());
    else
      return new ParallelVVector<TV> ( afespace.GetNDof(),& afespace.GetParallelDofs());
  }
#endif

  ///
  template <class TM, class TV>
  void T_BilinearFormSymmetric<TM,TV> :: 
  AddElementMatrix (const Array<int> & dnums1,
                    const Array<int> & dnums2,
                    const FlatMatrix<TSCAL> & elmat,
                    bool inner_element, int elnr,
                    LocalHeap & lh) 
  {
    TMATRIX & mat = dynamic_cast<TMATRIX&> (*this->mats.Last());

    // #pragma omp critical (addelmat)
    {
      mat.AddElementMatrix (dnums1, elmat);

#ifdef OLD
      for (int i = 0; i < dnums1.Size(); i++)
        for (int j = 0; j < dnums2.Size(); j++)
          if (dnums1[i] != -1 && dnums2[j] != -1 &&
              dnums1[i] >= dnums2[j])
            {

              AddPartOfElementMatrix(mat(dnums1[i], dnums2[j]),
                                     elmat,
                                     i,j);
              /*
                TM & mij = mat(dnums1[i], dnums2[j]);
                int hi = Height (mij);
                int wi = Width (mij);
	  
                for (int k = 0; k < hi; k++)
                for (int l = 0; l < wi; l++)
                mij(k,l) += elmat(i*hi+k, j*wi+l);
              */
            }
#endif
    }
  }





  template <> void T_BilinearFormSymmetric<double,double>::
  AddElementMatrix (const Array<int> & dnums1,
                    const Array<int> & dnums2,
                    const FlatMatrix<double> & elmat,
                    bool inner_element, int elnr,
                    LocalHeap & lh) 
  {
    TMATRIX & mat = dynamic_cast<TMATRIX&> (GetMatrix());

    // #pragma omp critical (addelmat)
    {
      mat.AddElementMatrix (dnums1, elmat);

      /*
	static int addtimer = NgProfiler::CreateTimer ("Element matrix insertion");
	NgProfiler::RegionTimer reg(addtimer);

	for (int i = 0; i < dnums1.Size(); i++)
        if (dnums1[i] != -1)
	for (int j = 0; j < dnums2.Size(); j++)
	if (dnums2[j] != -1 && dnums1[i] >= dnums2[j])
	AddPartOfElementMatrix(mat(dnums1[i], dnums2[j]), elmat, i,j);
      */
    }
    // mat(dnums1[i], dnums2[j]) += elmat(i, j);
  }

  template <> void T_BilinearFormSymmetric<Complex,Complex>::
  AddElementMatrix (const Array<int> & dnums1,
                    const Array<int> & dnums2,
                    const FlatMatrix<Complex> & elmat,
                    bool inner_element, int elnr,
                    LocalHeap & lh) 
  {
    TMATRIX & mat = dynamic_cast<TMATRIX&> (*mats.Last());
  
    // #pragma omp critical (addelmat)
    {
      static int addtimer = NgProfiler::CreateTimer ("Element matrix insertion, Complex-Complex");
      NgProfiler::RegionTimer reg(addtimer);

      mat.AddElementMatrix (dnums1, elmat);

      /*
	for (int i = 0; i < dnums1.Size(); i++)
        for (int j = 0; j < dnums2.Size(); j++)
	if (dnums1[i] != -1 && dnums2[j] != -1 && 
	dnums1[i] >= dnums2[j])
	AddPartOfElementMatrix(mat(dnums1[i], dnums2[j]),
	elmat,
	i,j);
      */
    }
    //mat(dnums1[i], dnums2[j]) += elmat(i, j);
  }

  template <> void T_BilinearFormSymmetric<double,Complex>::
  AddElementMatrix (const Array<int> & dnums1,
                    const Array<int> & dnums2,
                    const FlatMatrix<double> & elmat,
                    bool inner_element, int elnr,
                    LocalHeap & lh) 
  {
    TMATRIX & mat = dynamic_cast<TMATRIX&> (*mats.Last());
    // #pragma omp critical (addelmat)  
    {
      static int addtimer = NgProfiler::CreateTimer ("Element matrix insertion, double-Complex");
      NgProfiler::RegionTimer reg(addtimer);

      mat.AddElementMatrix (dnums1, elmat);
    
      /*
	for (int i = 0; i < dnums1.Size(); i++)
	for (int j = 0; j < dnums2.Size(); j++)
	if (dnums1[i] != -1 && dnums2[j] != -1 && 
	dnums1[i] >= dnums2[j])
	AddPartOfElementMatrix(mat(dnums1[i], dnums2[j]), elmat, i,j);
	//mat(dnums1[i], dnums2[j]) += elmat(i, j);
	*/
    }
  }


  template <class TM, class TV>
  void T_BilinearFormSymmetric<TM,TV>::LapackEigenSystem(FlatMatrix<TSCAL> & elmat, LocalHeap & lh) const
  {
    if ( !this->fespace.IsComplex() )
      {
        Vector<TSCAL> lami(elmat.Height());
        Matrix<TSCAL> evecs(elmat.Height());
#ifdef LAPACK
        LapackEigenValuesSymmetric (elmat, lami, evecs);
        (*testout) << "lami = " << endl << lami << endl << "evecs: " << endl << evecs << endl;
#endif
      }
    else
      {
        Vector<Complex> lami(elmat.Height());
        Matrix<TSCAL> evecs(elmat.Height());
        FlatMatrix<TSCAL> elmat_save(elmat.Height(), elmat.Width(), lh);
        elmat_save = elmat;
#ifdef LAPACK
        LapackEigenValues (elmat_save, lami, evecs);
        (*testout) << "LAPACK NS for complex symmetric problem \nlami = " 
                   << endl << lami << endl << "evecs: " << endl << evecs << endl;
#endif
      }
  }


  template <class TM, class TV>
  void T_BilinearFormSymmetric<TM,TV>::ApplyElementMatrix(const BaseVector & x,
                                                          BaseVector & y,
                                                          const TSCAL & val,
                                                          const Array<int> & dnums,
                                                          const ElementTransformation & eltrans,
                                                          const int elnum,
                                                          const int type,
                                                          int & cnt,
                                                          LocalHeap & lh,
                                                          const FiniteElement * fel,
                                                          const SpecialElement * sel) const
  {
    // FlatVector<TV> elvecx (dnums.Size() * this->GetFESpace().GetDimension(), lh);
    // FlatVector<TV> elvecy (dnums.Size() * this->GetFESpace().GetDimension(), lh);
    FlatVector<TSCAL> elvecx (dnums.Size() * this->GetFESpace().GetDimension(), lh);
    FlatVector<TSCAL> elvecy (dnums.Size() * this->GetFESpace().GetDimension(), lh);
		      
    x.GetIndirect (dnums, elvecx);

    if(type == 0 || type == 1)
      {
        this->fespace.TransformVec (elnum, (type == 1), elvecx, TRANSFORM_SOL);

        for (int j = 0; j < this->NumIntegrators(); j++)
          {
            BilinearFormIntegrator & bfi = *this->parts[j];
	    
            if (bfi.SkeletonForm()) continue;
            if (type == 0 && bfi.BoundaryForm()) continue;
            if (type == 0 && !bfi.DefinedOn (this->ma.GetElIndex (elnum))) continue;
            if (type == 1 && !bfi.BoundaryForm()) continue;
	    
	    
            static int elementtimer = NgProfiler::CreateTimer ("Element matrix application");
            NgProfiler::StartTimer (elementtimer);
	    
	    
            if (this->precompute)
              // bfi.ApplyElementMatrix (*fel, eltrans, elvecx, elvecy, this->precomputed_data[cnt++], lh);
              bfi.ApplyElementMatrix (*fel, eltrans, elvecx, elvecy, 
                                      this->precomputed_data[elnum*this->NumIntegrators()+j], lh);
            else
              bfi.ApplyElementMatrix (*fel, eltrans, elvecx, elvecy, 0, lh);
	    
            NgProfiler::StopTimer (elementtimer);
	    
            /*
              testout->precision (12);
              (*testout) << "el " << i << ", dom = " << ma.GetElIndex(i) << ",integrator = " << typeid(bfi).name() << endl
              << "elx = " << elvecx 
              << "ely = " << elvecy << endl;
            */
            BilinearForm::GetFESpace().TransformVec (elnum, (type == 1), elvecy, TRANSFORM_RHS);
	
            elvecy *= val;
	    // #pragma omp critical(addapply)
            {
              y.AddIndirect (dnums, elvecy);
            }
          }
      }
    else if (type == 2)
      {
        sel->Apply (elvecx, elvecy, lh);
        elvecy *= val;
        y.AddIndirect (dnums, elvecy);
      }
		      
  }







  template <class TM>
  T_BilinearFormDiagonal<TM> :: 
  T_BilinearFormDiagonal (const FESpace & afespace, const string & aname,
                          const Flags & flags)
    : S_BilinearForm<TSCAL> (afespace, aname, flags) 
  { 
    this->diagonal = 1;
    cout << " !!!!!!!!!!!!!!!!!! allocated diagonal matrix !!!!!!!!!!!!!" << endl;

    if (&this->fespace.LowOrderFESpace())
      {
        this->low_order_bilinear_form = 
          new T_BilinearFormSymmetric<TM> (this->fespace.LowOrderFESpace(),
                                           aname+string(" low-order"), flags);
        this->low_order_bilinear_form -> SetDiagonal (0);
      }
  }

  template <class TM>
  T_BilinearFormDiagonal<TM> :: 
  ~T_BilinearFormDiagonal ()
  {
    for (int i = 0; i < this->mats.Size(); i++)
      delete this->mats[i];
  }

  ///
  template <class TM>
  void T_BilinearFormDiagonal<TM> :: 
  AllocateMatrix ()
  {
    if (this->mats.Size() == this->ma.GetNLevels())
      return;

    int ndof = this->fespace.GetNDof();
    MatrixGraph * graph = new MatrixGraph (ndof, 1);
    for (int i = 0; i < ndof; i++)
      graph->CreatePosition (i, i);

    // graphs.Append (graph);
    this->mats.Append (new SparseMatrixSymmetric<TM> (*graph, 1));
    delete graph;

    if (!this->multilevel || this->low_order_bilinear_form)
      for (int i = 0; i < this->mats.Size()-1; i++)
        {
          delete this->mats[i];
          this->mats[i] = 0;
        }
  }


#ifdef PARALLEL
  template <class TM>
  void T_BilinearFormDiagonal<TM>::
  AllocateConsistentMatrix ()
  {
    if ( id > 0 )
      {
        int ndof = this->fespace.GetNDof();
        MatrixGraph * consistentgraph = new MatrixGraph (ndof, 1);
        for (int i = 0; i < ndof; i++)
          consistentgraph->CreatePosition (i, i);
        ParallelBaseMatrix& pbm = dynamic_cast<ParallelBaseMatrix&>(*this->mats.Last());
        pbm . AllocateConsistentMat(*consistentgraph);
        delete consistentgraph;
      }     
    /*
      if ( id > 0 )
      {
      ParallelBaseMatrix& pbm = dynamic_cast<ParallelBaseMatrix&>(*this->mats.Last());
      pbm . AllocateConsistentMat();
      }     
    */
  }
#endif


#ifndef PARALLEL
  template <class TM>
  BaseVector * T_BilinearFormDiagonal<TM> :: 
  CreateVector() const
  {
    return new VVector<TV_COL> (this->fespace.GetNDof());
  }
#else
  template <class TM>
  BaseVector * T_BilinearFormDiagonal<TM> :: 
  CreateVector() const
  {
    const FESpace & afespace = this->fespace;
    if ( &afespace.GetParallelDofs() == 0 )
      return new VVector<TV_COL> (afespace.GetNDof());
    else
      return new ParallelVVector<TV_COL> ( afespace.GetNDof(),& afespace.GetParallelDofs());
  }
#endif


  ///
  template <class TM>
  void T_BilinearFormDiagonal<TM> :: 
  AddElementMatrix (const Array<int> & dnums1,
                    const Array<int> & dnums2,
                    const FlatMatrix<TSCAL> & elmat,
                    bool inner_element, int elnr,
                    LocalHeap & lh) 
  {
    TMATRIX & mat = dynamic_cast<TMATRIX&> (*this->mats.Last());

    // #pragma omp critical (addelmat)
    {

      for (int i = 0; i < dnums1.Size(); i++)
        if (dnums1[i] != -1)
          {
            TM & mij = mat(dnums1[i], dnums1[i]);
            int hi = Height (mij);
            int wi = Width (mij);
	  
            for (int k = 0; k < hi; k++)
              for (int l = 0; l < wi; l++)
                mij(k,l) += elmat(i*hi+k, i*wi+l);
          }
    }
  }



  ///
  template <> void T_BilinearFormDiagonal<double>::
  AddElementMatrix (const Array<int> & dnums1,
                    const Array<int> & dnums2,
                    const FlatMatrix<double> & elmat,
                    bool inner_element, int elnr,
                    LocalHeap & lh) 
  {
    TMATRIX & mat = dynamic_cast<TMATRIX&> (GetMatrix());

    // #pragma omp critical (addelmat)
    {
      for (int i = 0; i < dnums1.Size(); i++)
        if (dnums1[i] != -1)
          mat(dnums1[i], dnums1[i]) += elmat(i, i);
    }
  }




  ///
  template <> void T_BilinearFormDiagonal<Complex>::
  AddElementMatrix (const Array<int> & dnums1,
                    const Array<int> & dnums2,
                    const FlatMatrix<Complex> & elmat,
                    bool inner_element, int elnr,
                    LocalHeap & lh) 
  {
    TMATRIX & mat = dynamic_cast<TMATRIX&> (GetMatrix()); 

    // #pragma omp critical (addelmat)
    {
      for (int i = 0; i < dnums1.Size(); i++)
        if (dnums1[i] != -1)
          mat(dnums1[i], dnums1[i]) += elmat(i, i);
    }
  }



  template <class TM >
  void T_BilinearFormDiagonal<TM>::ApplyElementMatrix(const BaseVector & x,
                                                      BaseVector & y,
                                                      const TSCAL & val,
                                                      const Array<int> & dnums,
                                                      const ElementTransformation & eltrans,
                                                      const int elnum,
                                                      const int type,
                                                      int & cnt,
                                                      LocalHeap & lh,
                                                      const FiniteElement * fel,
                                                      const SpecialElement * sel) const
  {
    FlatVector<typename mat_traits<TM>::TV_ROW > elvecx (dnums.Size() * this->GetFESpace().GetDimension(), lh);
    FlatVector<typename mat_traits<TM>::TV_COL > elvecy (dnums.Size() * this->GetFESpace().GetDimension(), lh);
		      
    x.GetIndirect (dnums, elvecx);

    if(type == 0 || type == 1)
      {
        this->fespace.TransformVec (elnum, (type == 1), elvecx, TRANSFORM_SOL);

        for (int j = 0; j < this->NumIntegrators(); j++)
          {
            BilinearFormIntegrator & bfi = *this->parts[j];
            if (bfi.SkeletonForm()) continue;
            if (type == 0 && bfi.BoundaryForm()) continue;
            if (type == 0 && !bfi.DefinedOn (this->ma.GetElIndex (elnum))) continue;
            if (type == 1 && !bfi.BoundaryForm()) continue;
	    
	    
            static int elementtimer = NgProfiler::CreateTimer ("Element matrix application");
            NgProfiler::StartTimer (elementtimer);
	    
	    
            if (this->precompute)
              bfi.ApplyElementMatrix (*fel, eltrans, elvecx, elvecy, this->precomputed_data[cnt++], lh);
            else
              bfi.ApplyElementMatrix (*fel, eltrans, elvecx, elvecy, 0, lh);
	    
            NgProfiler::StopTimer (elementtimer);
	    
            /*
              testout->precision (12);
              (*testout) << "el " << i << ", dom = " << ma.GetElIndex(i) << ",integrator = " << typeid(bfi).name() << endl
              << "elx = " << elvecx 
              << "ely = " << elvecy << endl;
            */
            BilinearForm::GetFESpace().TransformVec (elnum, (type == 1), elvecy, TRANSFORM_RHS);
	
            elvecy *= val;
            y.AddIndirect (dnums, elvecy);
          }
      }
    else if (type == 2)
      {
        sel->Apply (elvecx, elvecy, lh);
        elvecy *= val;
        y.AddIndirect (dnums, elvecy);
      }
		      
  }



  template <class TM>
  void T_BilinearFormDiagonal<TM> :: 
  AddDiagElementMatrix (const Array<int> & dnums1,
                        const FlatVector<TSCAL> & diag,
                        bool inner_element, int elnr,
                        LocalHeap & lh) 
  {
    throw Exception ("generic AddDiagElementMatrix not implemented");
    /*
      TMATRIX & mat = dynamic_cast<TMATRIX&> (*this->mats.Last());
    
      for (int i = 0; i < dnums1.Size(); i++)
      if (dnums1[i] != -1)
      {
      TM mij = diag(dnums1[i]);
      int hi = Height (mij);
      int wi = Width (mij);
	  
      for (int k = 0; k < hi; k++)
      for (int l = 0; l < wi; l++)
      mij(k,l) += elmat(i*hi+k, i*wi+l);
      }
    */
  }



  ///
  template <> void T_BilinearFormDiagonal<double>::
  AddDiagElementMatrix (const Array<int> & dnums1,
                        const FlatVector<double> & diag,
                        bool inner_element, int elnr,
                        LocalHeap & lh) 
  {
    TMATRIX & mat = dynamic_cast<TMATRIX&> (GetMatrix());

    for (int i = 0; i < dnums1.Size(); i++)
      if (dnums1[i] != -1)
        mat(dnums1[i], dnums1[i]) += diag(i);
  }

  ///
  template <> void T_BilinearFormDiagonal<Complex>::
  AddDiagElementMatrix (const Array<int> & dnums1,
                        const FlatVector<Complex> & diag,
                        bool inner_element, int elnr,
                        LocalHeap & lh) 
  {
    TMATRIX & mat = dynamic_cast<TMATRIX&> (GetMatrix()); 

    for (int i = 0; i < dnums1.Size(); i++)
      if (dnums1[i] != -1)
        mat(dnums1[i], dnums1[i]) += diag(i);
  }






















  BilinearForm * CreateBilinearForm (const FESpace * space,
                                     const string & name,
                                     const Flags & flags)
  {
    BilinearForm * bf = 0;

    if (flags.GetDefineFlag ("ebe")){
      if ( space->IsComplex() )
	return new ElementByElement_BilinearForm<Complex> (*space, name, flags);
      else 
	return new ElementByElement_BilinearForm<double> (*space, name, flags);
    }
    
    if (flags.GetDefineFlag ("symmetric"))
      {

        if (space->IsComplex() && flags.GetDefineFlag ("real"))
          {
            if(flags.NumFlagDefined("cacheblocksize"))
              {
                switch(int(flags.GetNumFlag("cacheblocksize",1)))
                  {
#if MAX_CACHEBLOCKS >= 2
                  case 2:
                    return new T_BilinearFormSymmetric<double,Vec<2,Complex> > (*space, name, flags);
#endif
#if MAX_CACHEBLOCKS >= 3
                  case 3:
                    return new T_BilinearFormSymmetric<double,Vec<3,Complex> > (*space, name, flags);
                  case 4:
                    return new T_BilinearFormSymmetric<double,Vec<4,Complex> > (*space, name, flags);
#endif
#if MAX_CACHEBLOCKS >= 5
                  case 5:
                    return new T_BilinearFormSymmetric<double,Vec<5,Complex> > (*space, name, flags);
                  case 6:
                    return new T_BilinearFormSymmetric<double,Vec<6,Complex> > (*space, name, flags);
                  case 7:
                    return new T_BilinearFormSymmetric<double,Vec<7,Complex> > (*space, name, flags);
                  case 8:
                    return new T_BilinearFormSymmetric<double,Vec<8,Complex> > (*space, name, flags);
                  case 9:
                    return new T_BilinearFormSymmetric<double,Vec<9,Complex> > (*space, name, flags);
                  case 10:
                    return new T_BilinearFormSymmetric<double,Vec<10,Complex> > (*space, name, flags);
                  case 11:
                    return new T_BilinearFormSymmetric<double,Vec<11,Complex> > (*space, name, flags);
                  case 12:
                    return new T_BilinearFormSymmetric<double,Vec<12,Complex> > (*space, name, flags);
                  case 13:
                    return new T_BilinearFormSymmetric<double,Vec<13,Complex> > (*space, name, flags);
                  case 14:
                    return new T_BilinearFormSymmetric<double,Vec<14,Complex> > (*space, name, flags);
                  case 15:
                    return new T_BilinearFormSymmetric<double,Vec<15,Complex> > (*space, name, flags);
#endif
                  }
              }
            else
              return new T_BilinearFormSymmetric<double,Complex> (*space, name, flags);
          }

        if(flags.NumFlagDefined("cacheblocksize"))
          {
            CreateSymMatObject4 (bf, T_BilinearFormSymmetric, 
                                 space->GetDimension(),
                                 int(flags.GetNumFlag("cacheblocksize",1)),
                                 space->IsComplex(),   
                                 *space, name, flags);
          }
        else
          CreateSymMatObject3 (bf, T_BilinearFormSymmetric, 
                               space->GetDimension(), space->IsComplex(),   
                               *space, name, flags);
      }
    else if (flags.GetDefineFlag ("diagonal"))
      {
        CreateSymMatObject3 (bf, T_BilinearFormDiagonal, 
                             space->GetDimension(), space->IsComplex(),   
                             *space, name, flags);
      }
    else
      {
        if (space->IsComplex() && flags.GetDefineFlag ("real"))
          {
	    
            if(flags.NumFlagDefined("cacheblocksize"))
              {
                switch(int(flags.GetNumFlag("cacheblocksize",1)))
                  {
#if MAX_CACHEBLOCKS >= 2
                  case 2:
                    return new T_BilinearForm<double,Vec<2,Complex> > (*space, name, flags);
#endif
#if MAX_CACHEBLOCKS >= 3
                  case 3:
                    return new T_BilinearForm<double,Vec<3,Complex> > (*space, name, flags);
                  case 4:
                    return new T_BilinearForm<double,Vec<4,Complex> > (*space, name, flags);
#endif
#if MAX_CACHEBLOCKS >= 5
                  case 5:
                    return new T_BilinearForm<double,Vec<5,Complex> > (*space, name, flags);
                  case 6:
                    return new T_BilinearForm<double,Vec<6,Complex> > (*space, name, flags);
                  case 7:
                    return new T_BilinearForm<double,Vec<7,Complex> > (*space, name, flags);
                  case 8:
                    return new T_BilinearForm<double,Vec<8,Complex> > (*space, name, flags);
                  case 9:
                    return new T_BilinearForm<double,Vec<9,Complex> > (*space, name, flags);
                  case 10:
                    return new T_BilinearForm<double,Vec<10,Complex> > (*space, name, flags);
                  case 11:
                    return new T_BilinearForm<double,Vec<11,Complex> > (*space, name, flags);
                  case 12:
                    return new T_BilinearForm<double,Vec<12,Complex> > (*space, name, flags);
                  case 13:
                    return new T_BilinearForm<double,Vec<13,Complex> > (*space, name, flags);
                  case 14:
                    return new T_BilinearForm<double,Vec<14,Complex> > (*space, name, flags);
                  case 15:
                    return new T_BilinearForm<double,Vec<15,Complex> > (*space, name, flags);
#endif
                  }
              }
            else
              return new T_BilinearForm<double,Complex> (*space, name, flags);
          }

        if(flags.NumFlagDefined("cacheblocksize"))
          {
            CreateSymMatObject4 (bf, T_BilinearForm, 
                                 space->GetDimension(),
                                 int(flags.GetNumFlag("cacheblocksize",1)),
                                 space->IsComplex(),   
                                 *space, name, flags);
          }
        else
          CreateSymMatObject3 (bf, T_BilinearForm, 
                               space->GetDimension(), space->IsComplex(),   
                               *space, name, flags);
      }

    return bf;
  }










  class ApplyFineMatrix : public BaseMatrix
  {
    const BaseMatrix & finemat;
    const ngmg::Prolongation & prol;
    int level;
    
  public:
    ApplyFineMatrix (const BaseMatrix & afinemat,
                     const ngmg::Prolongation & aprol,
                     int alevel);
  
    virtual void Mult (const BaseVector & x, BaseVector & y) const;
    virtual BaseVector * CreateVector () const;
  };

  ApplyFineMatrix :: 
  ApplyFineMatrix (const BaseMatrix & afinemat,
                   const ngmg::Prolongation & aprol,
                   int alevel)
    : finemat(afinemat), prol(aprol), level(alevel)
  {
    ;
  }

  void ApplyFineMatrix :: 
  Mult (const BaseVector & x, BaseVector & y) const
  {
    /*
      BaseVector & fx = *finemat.CreateVector();
      BaseVector & fy = *finemat.CreateVector();
  
      fx.SetScalar (0);
      fx.AddPart (1, 1, x);
      //  prol.ProlongateInline (level, fx);
      finemat.Mult (fx, fy);
      //  prol.RestrictInline (level, fy);
      fy.GetPart (1, y);

      delete &fx;
      delete &fy;
    */
    cout << "Apply Matrix currently not implemented" << endl;
  }

  BaseVector * ApplyFineMatrix :: CreateVector () const
  {
    cerr << "ApplyFineMatrix::CreateVector:  Need Help !!!" << endl;
    return NULL;
  }


  void BilinearForm :: GalerkinProjection ()
  {
    // old code:
    /*
      for (int i = GetNLevels()-1; i >= 1; i--)
      {
      ApplyFineMatrix afm (GetMatrix(i+1),
      *GetFESpace().GetProlongation(),
      i+1);
			   
      //      const_cast<BaseMatrix&> (GetMatrix(i)).MakeMatrixFromOperator (afm);
      }
    */

    const ngmg::Prolongation* prol = fespace.GetProlongation();
    SparseMatrix<double>* prolMat = NULL;

    if ( !low_order_bilinear_form )
      /*
        for( int i=GetNLevels()-1; i>0; i-- )
        {
        prolMat = prol->CreateProlongationMatrix( i ) ;

        low_order_bilinear_form->GetMatrix( i-1 ) =
        *( dynamic_cast< const BaseSparseMatrix& >( low_order_bilinear_form->GetMatrix( i ) ).Restrict
        ( *prolMat, &( dynamic_cast< BaseSparseMatrix& >
        ( low_order_bilinear_form->GetMatrix( i-1 ) ) ) ) );
        delete prolMat;
        }
        else
      */
      for( int i=GetNLevels()-1; i>0; i-- )
        {
          prolMat = prol->CreateProlongationMatrix( i );
	     
          GetMatrix( i-1 ) = 
            *( dynamic_cast< const BaseSparseMatrix& >( GetMatrix( i ) ).
               Restrict( *prolMat, &( dynamic_cast< BaseSparseMatrix& >
                                      ( GetMatrix( i-1 ) ) ) ) );
          delete prolMat;
        }
      
  }

  
  BilinearFormApplication :: 
  BilinearFormApplication (const BilinearForm * abf)
    : bf (abf)
  {
    ;
  }

  void BilinearFormApplication :: 
  Mult (const BaseVector & v, BaseVector & prod) const
  {
#ifdef PARALLEL
    if ( ntasks > 1 )
      {
	const ParallelBaseVector * parv = dynamic_cast<const ParallelBaseVector*> (&v);
	if (parv)
	  parv -> AllReduce(&hoprocs);
      }
#endif

    prod = 0;
    bf -> AddMatrix (1, v, prod);

#ifdef PARALLEL
    if ( ntasks > 1 )
      {
	ParallelBaseVector * parprod = dynamic_cast<ParallelBaseVector*> (&prod);
	if (parprod)
	  parprod->SetStatus(DISTRIBUTED);
      }
#endif
  }

  void BilinearFormApplication :: 
  MultAdd (double val, const BaseVector & v, BaseVector & prod) const
  {
#ifdef PARALLEL
    if ( ntasks > 1 )
      {
	const ParallelBaseVector * parv = dynamic_cast<const ParallelBaseVector*> (&v);
	if (parv)
	  parv -> AllReduce(&hoprocs);
      }
#endif

    bf -> AddMatrix (val, v, prod);


#ifdef PARALLEL
    if ( ntasks > 1 )
      {
	ParallelBaseVector * parprod = dynamic_cast<ParallelBaseVector*> (&prod);
	if (parprod)
	  parprod->SetStatus(DISTRIBUTED);
      }
#endif
  }

  void BilinearFormApplication :: 
  MultAdd (Complex val, const BaseVector & v, BaseVector & prod) const
  {
#ifdef PARALLEL
    if ( ntasks > 1 )
      {
	const ParallelBaseVector * parv = dynamic_cast<const ParallelBaseVector*> (&v);
	if (parv)
	  parv -> AllReduce(&hoprocs);
      }
#endif

    bf -> AddMatrix (val, v, prod);


#ifdef PARALLEL
    if ( ntasks > 1 )
      {
	ParallelBaseVector * parprod = dynamic_cast<ParallelBaseVector*> (&prod);
	if (parprod)
	  parprod->SetStatus(DISTRIBUTED);
      }
#endif
  }

  /*
    void BilinearFormApplication :: 
    MultTransAdd (double val, const BaseVector & v, BaseVector & prod) const
    {
    bf -> AddMatrix (val, v, prod);
    }
  */


  BaseVector * BilinearFormApplication :: 
  CreateVector () const
  {
    return bf -> CreateVector();
  }




  LinearizedBilinearFormApplication ::
  LinearizedBilinearFormApplication (const BilinearForm * abf,
                                     const BaseVector * aveclin)
    : BilinearFormApplication (abf), veclin(aveclin)
  {
    ;
  }

  void  LinearizedBilinearFormApplication :: 
  Mult (const BaseVector & v, BaseVector & prod) const
  {
    prod = 0;
    bf->ApplyLinearizedMatrixAdd (1, *veclin, v, prod);
  }

  void LinearizedBilinearFormApplication :: 
  MultAdd (double val, const BaseVector & v, BaseVector & prod) const
  {
    bf->ApplyLinearizedMatrixAdd (val, *veclin, v, prod);
  }

  void LinearizedBilinearFormApplication :: 
  MultAdd (Complex val, const BaseVector & v, BaseVector & prod) const
  {
    bf->ApplyLinearizedMatrixAdd (val, *veclin, v, prod);
  }




  template <class SCAL>
  ElementByElement_BilinearForm<SCAL> :: 
  ElementByElement_BilinearForm (const FESpace & afespace, const string & aname,
				 const Flags & flags)
    : S_BilinearForm<SCAL> (afespace, aname, flags)
  { ; }

  template <class SCAL>
  ElementByElement_BilinearForm<SCAL> :: ~ElementByElement_BilinearForm ()
  { ; }



  
  template <class SCAL>
  void ElementByElement_BilinearForm<SCAL> :: AllocateMatrix ()
  {
    cout << "alloc matrix" << endl;
    const FESpace & fespace = this->fespace;
    this->mats.Append (new ElementByElementMatrix<SCAL> (fespace.GetNDof(), this->ma.GetNE()+this->ma.GetNSE() ));
  }


  template<class SCAL>
  BaseVector * ElementByElement_BilinearForm<SCAL> :: CreateVector() const
  {
    return new VVector<SCAL> (this->fespace.GetNDof());
  }

  template<class SCAL>
  void ElementByElement_BilinearForm<SCAL> :: AddElementMatrix (const Array<int> & dnums1,
								const Array<int> & dnums2,
								const FlatMatrix<SCAL> & elmat,
								bool inner_element, int elnr,
								LocalHeap & lh)
  {
    /*
      (*testout) << "inner_element = " << inner_element << endl;
      (*testout) << "elnr = " << elnr << endl;
      (*testout) << "elmat = " << endl << elmat << endl;
      (*testout) << "dnums1 = " << endl << dnums1 << endl;
    */
    int nr = elnr;
    if (!inner_element) nr += this->ma.GetNE();
    dynamic_cast<ElementByElementMatrix<SCAL>&> (this->GetMatrix()) . AddElementMatrix (nr, dnums1, dnums2, elmat);
  }
  

  template class ElementByElement_BilinearForm<double>;
  template class ElementByElement_BilinearForm<Complex>;






}
