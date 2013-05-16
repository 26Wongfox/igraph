/* -*- mode: C -*-  */
/* 
   IGraph library.
   Copyright (C) 2012  Gabor Csardi <csardi.gabor@gmail.com>
   334 Harvard street, Cambridge, MA 02139 USA
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 
   02110-1301 USA

*/

/*
  The contents of this file was originally taken from the LAD 
  homepage: http://liris.cnrs.fr/csolnon/LAD.html and then 
  modified to fit better into igraph.

  Unfortunately LAD seems to have no version numbers. The files
  were apparently last changed on the 29th of June, 2010.
  
  The original copyright message follows here. The CeCILL-B V1 license
  is GPL compatible, because instead of V1, one can freely choose to 
  use V2, and V2 is explicitly GPL compatible.
*/

/* This software has been written by Christine Solnon.
   It is distributed under the CeCILL-B FREE SOFTWARE LICENSE
   see http://www.cecill.info/licences/Licence_CeCILL-B_V1-en.html 
   for more details
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>
#include <time.h>
#include <limits.h>

#include "igraph_interface.h"
#include "igraph_adjlist.h"
#include "igraph_vector.h"
#include "igraph_vector_ptr.h"
#include "igraph_memory.h"
#include "igraph_matrix.h"
#include "igraph_interrupt_internal.h"

/* define boolean type as char */
#define true 1
#define false 0
#define bool char

/* ---------------------------------------------------------*/
/* Coming from graph.c                                      */
/* ---------------------------------------------------------*/

typedef struct{
  long int nbVertices; /* Number of vertices */
  igraph_vector_t nbSucc;
  igraph_adjlist_t succ;
  igraph_matrix_char_t isEdge;
} Tgraph;

int igraph_i_lad_createGraph(const igraph_t *igraph, Tgraph* graph) {
  long int i, j, n;
  long int no_of_nodes=igraph_vcount(igraph);
  igraph_vector_t *neis;
  
  IGRAPH_VECTOR_INIT_FINALLY(&graph->nbSucc, no_of_nodes);
  IGRAPH_CHECK(igraph_degree(igraph, &graph->nbSucc, igraph_vss_all(), 
			     IGRAPH_OUT, IGRAPH_LOOPS));
  
  graph->nbVertices = no_of_nodes;

  IGRAPH_CHECK(igraph_adjlist_init(igraph, &graph->succ, IGRAPH_OUT));
  IGRAPH_FINALLY(igraph_adjlist_destroy, &graph->succ);
  IGRAPH_CHECK(igraph_matrix_char_init(&graph->isEdge, 
				       no_of_nodes, no_of_nodes));
  IGRAPH_FINALLY(igraph_matrix_char_destroy, &graph->isEdge);
	
  for (i=0; i<no_of_nodes; i++) {
    neis=igraph_adjlist_get(&graph->succ, i);
    n=igraph_vector_size(neis);
    for (j=0; j<n; j++) {
      int v=(int)VECTOR(*neis)[j];
      if (MATRIX(graph->isEdge, i, v)) {
	IGRAPH_ERROR("LAD functions only work on simple graphs, "
		     "simplify your graph", IGRAPH_EINVAL);
      }
      MATRIX(graph->isEdge, i, v) = 1;
    }
  }

  return 0;
}

/* ---------------------------------------------------------*/
/* Coming from domains.c                                    */
/* ---------------------------------------------------------*/

typedef struct{
  igraph_vector_int_t nbVal;    /* nbVal[u] = number of values in D[u] */
  igraph_vector_int_t firstVal; /* firstVal[u] = pos in val of the
				   first value of D[u] */
  igraph_vector_int_t val;      /* val[firstVal[u]..firstVal[u]+nbVal[u]-1] = 
				   values of D[u] */
  igraph_matrix_int_t posInVal; 
  /* If v in D[u] then firstVal[u] <= posInVal[u][v] < firstVal[u]+nbVal[u] 
     and val[posInVal[u][v]] = v
     otherwise posInVal[u][v] >= firstVal[u]+nbVal[u] */
  int valSize;    /* size of val */
  igraph_matrix_int_t firstMatch; 
  /* firstMatch[u][v] = pos in match of the first vertex 
     of the covering matching of G_(u, v) */
  igraph_vector_int_t matching; 
  /* matching[firstMatch[u][v]..firstMatch[u][v]+nbSucc[u]-1]
     = covering matching of G_(u, v) */
  int nextOutToFilter; /* position in toFilter of the next pattern node whose
			  domain should be filtered (-1 if no domain to 
			  filter) */
  int lastInToFilter; /* position in toFilter of the last pattern node whose 
			 domain should be filtered */
  igraph_vector_int_t toFilter;  /* contain all pattern nodes whose 
				    domain should be filtered */
  igraph_vector_char_t markedToFilter;  /* markedToFilter[u]=true if u
					   is in toFilter; false otherwise */
  igraph_vector_int_t globalMatchingP; /* globalMatchingP[u] = node of Gt
					  matched to u in globalAllDiff(Np) */
  igraph_vector_int_t globalMatchingT; 
  /* globalMatchingT[v] = node of Gp matched to v in globalAllDiff(Np)
     or -1 if v is not matched */
} Tdomain;

bool igraph_i_lad_toFilterEmpty(Tdomain* D) {
  /* return true if there is no more nodes in toFilter */
  return (D->nextOutToFilter < 0);
}

void igraph_i_lad_resetToFilter(Tdomain *D) {
  /* empty to filter and unmark the vertices that are marked to be filtered */
  igraph_vector_char_null(&D->markedToFilter);
  D->nextOutToFilter = -1;
}


int igraph_i_lad_nextToFilter(Tdomain* D, int size) {
  /* precondition: emptyToFilter = false 
     remove a node from toFilter (FIFO)
     unmark this node and return it */
  int u = VECTOR(D->toFilter)[D->nextOutToFilter];
  VECTOR(D->markedToFilter)[u] = false;
  if (D->nextOutToFilter == D->lastInToFilter) {
    /* u was the last node in tofilter */
    D->nextOutToFilter = -1;
  } else if (D->nextOutToFilter == size-1) {
    D->nextOutToFilter = 0;
  } else {
    D->nextOutToFilter++;
  }
  return u;
}

void igraph_i_lad_addToFilter(int u, Tdomain* D, int size) {
  /* if u is not marked, then add it to toFilter and mark it */
  if (VECTOR(D->markedToFilter)[u]) { return; }
  VECTOR(D->markedToFilter)[u] = true;
  if (D->nextOutToFilter < 0) {
    D->lastInToFilter = 0;
    D->nextOutToFilter = 0;
  } else if (D->lastInToFilter == size-1) {
    D->lastInToFilter = 0;
  } else {
    D->lastInToFilter++;
  }
  VECTOR(D->toFilter)[D->lastInToFilter] = u;
}

bool igraph_i_lad_isInD(int u, int v, Tdomain* D) {
  /* returns true if v belongs to D(u); false otherwise */
  return (MATRIX(D->posInVal, u, v) < 
	  VECTOR(D->firstVal)[u] + VECTOR(D->nbVal)[u]);
}

bool igraph_i_lad_augmentingPath(int u, Tdomain* D, int nbV) {
  /* return true if there exists an augmenting path starting from u and 
     ending on a free vertex v in the bipartite directed graph G=(U,
     V, E) such that U=pattern nodes, V=target nodes, and 
     E={(u, v), v in D(u)} U {(v, u), D->globalMatchingP[u]=v}
     update D-globalMatchingP and D->globalMatchingT consequently */
  int fifo[nbV];
  int pred[nbV];
  int nextIn = 0;
  int nextOut = 0;
  int i, v, v2, u2, j;
  bool marked[nbV];
  memset(marked, false, nbV);
  for (i=0; i < VECTOR(D->nbVal)[u]; i++) {
    v = VECTOR(D->val)[ VECTOR(D->firstVal)[u]+i ];  /* v in D(u) */
    if (VECTOR(D->globalMatchingT)[v] < 0) { 
      /* v is free => augmenting path found */
      VECTOR(D->globalMatchingP)[u]=v;
      VECTOR(D->globalMatchingT)[v]=u;
      return true;
    }
    /* v is not free => add it to fifo */
    pred[v] = u;
    fifo[nextIn++] = v;
    marked[v] = true;
  }
  while (nextOut < nextIn) {
    u2 = VECTOR(D->globalMatchingT)[fifo[nextOut++]];
    for (i=0; i < VECTOR(D->nbVal)[u2]; i++) {
      v = VECTOR(D->val)[ VECTOR(D->firstVal)[u2]+i ]; /* v in D(u2) */
      if (VECTOR(D->globalMatchingT)[v] < 0) { 
	/* v is free => augmenting path found */
	j=0;
	while (u2 != u) { /* update global matching wrt path */
	  if (j>100) { exit(1); }
	  j++;
	  v2 = VECTOR(D->globalMatchingP)[u2];
	  VECTOR(D->globalMatchingP)[u2]=v;
	  VECTOR(D->globalMatchingT)[v]=u2;
	  v = v2;
	  u2 = pred[v];
	}
	VECTOR(D->globalMatchingP)[u]=v;
	VECTOR(D->globalMatchingT)[v]=u;
	return true;
      }
      if (!marked[v]) { /* v is not free and not marked => add it to fifo */
	pred[v] = u2;
	fifo[nextIn++] = v;
	marked[v] = true;
      }
    }
  }
  return false;
}

bool igraph_i_lad_removeAllValuesButOne(int u, int v, Tdomain* D, Tgraph* Gp,
					Tgraph* Gt) {
  /* remove all values but v from D(u) and add all successors of u in
     toFilter return false if an inconsistency is detected wrt to
     global all diff */
  int j, oldPos, newPos;
  igraph_vector_t *uneis=igraph_adjlist_get(&Gp->succ, u);
  int n=(int) igraph_vector_size(uneis);
  /* add all successors of u in toFilter */
  for (j=0; j<n; j++) {
    igraph_i_lad_addToFilter((int) VECTOR(*uneis)[j], D, 
			     (int) (Gp->nbVertices));
  }
  /* remove all values but v from D[u] */
  oldPos = MATRIX(D->posInVal, u, v);
  newPos = VECTOR(D->firstVal)[u];
  VECTOR(D->val)[oldPos] = VECTOR(D->val)[newPos];
  VECTOR(D->val)[newPos] = v;
  MATRIX(D->posInVal, u, VECTOR(D->val)[newPos]) = newPos;
  MATRIX(D->posInVal, u, VECTOR(D->val)[oldPos]) = oldPos;
  VECTOR(D->nbVal)[u] = 1;
  /* update global matchings that support the global all different
     constraint */
  if (VECTOR(D->globalMatchingP)[u] != v) {
    VECTOR(D->globalMatchingT)[ VECTOR(D->globalMatchingP)[u] ]=-1;
    VECTOR(D->globalMatchingP)[u] = -1;
    return igraph_i_lad_augmentingPath(u, D, (int) (Gt->nbVertices));
  }
  return true;
}


bool igraph_i_lad_removeValue(int u, int v, Tdomain* D, Tgraph* Gp,
			      Tgraph* Gt) {
  /* remove v from D(u) and add all successors of u in toFilter
     return false if an inconsistency is detected wrt global all diff */
  int j;
  igraph_vector_t *uneis=igraph_adjlist_get(&Gp->succ, u);
  int n=(int) igraph_vector_size(uneis);
  /* add all successors of u in toFilter */
  for (j=0; j<n; j++) {
    igraph_i_lad_addToFilter((int) VECTOR(*uneis)[j], D,
			     (int) (Gp->nbVertices));
  }
  /* remove v from D[u] */
  int oldPos = MATRIX(D->posInVal, u, v);
  VECTOR(D->nbVal)[u]--;
  int newPos = VECTOR(D->firstVal)[u] + VECTOR(D->nbVal)[u];
  VECTOR(D->val)[oldPos] = VECTOR(D->val)[newPos];
  VECTOR(D->val)[newPos] = v;
  MATRIX(D->posInVal, u, VECTOR(D->val)[oldPos]) = oldPos;
  MATRIX(D->posInVal, u, VECTOR(D->val)[newPos]) = newPos;
  /* update global matchings that support the global all different
     constraint */
  if (VECTOR(D->globalMatchingP)[u] == v) {
    VECTOR(D->globalMatchingP)[u] = -1;
    VECTOR(D->globalMatchingT)[v] = -1;
    return igraph_i_lad_augmentingPath(u, D, (int) (Gt->nbVertices));
  }
  return true;
}


int igraph_i_lad_matchVertices(int nb, igraph_vector_int_t* toBeMatched,
			       bool induced, Tdomain* D, Tgraph* Gp,
			       Tgraph* Gt, int *invalid) {
  /* for each u in toBeMatched[0..nb-1], match u to
     D->val[D->firstVal[u] and filter domains of other non matched
     vertices wrt FC(Edges) and FC(diff) (this is not mandatory, as
     LAD is stronger than FC(Edges) and GAC(allDiff) is stronger than
     FC(diff), but this speeds up the solution process). 
     return false if an inconsistency is detected by FC(Edges) or
     FC(diff); true otherwise; */
  int j, u, v, u2, oldNbVal;
  igraph_vector_t *vneis;
  while (nb>0) {
    u = VECTOR(*toBeMatched)[--nb];
    v = VECTOR(D->val)[ VECTOR(D->firstVal)[u] ]; 
    vneis = igraph_adjlist_get(&Gt->succ, v);
    /* match u to v */
    for (u2=0; u2<Gp->nbVertices; u2++) {
      if (u != u2) {
	oldNbVal = VECTOR(D->nbVal)[u2];
	if (igraph_i_lad_isInD(u2, v, D) && 
	    !igraph_i_lad_removeValue(u2, v, D, Gp, Gt)) {
	  *invalid = 1 ; return 0;
	}
	if (MATRIX(Gp->isEdge, u, u2)) { 
	  /* remove from D[u2] vertices which are not adjacent to v */
	  j = VECTOR(D->firstVal)[u2]; 
	  while (j < VECTOR(D->firstVal)[u2] + VECTOR(D->nbVal)[u2]) {
	    if (MATRIX(Gt->isEdge, v, VECTOR(D->val)[j])) { 
	      j++; 
	    } else if (!igraph_i_lad_removeValue(u2, VECTOR(D->val)[j], D, Gp, Gt)) { 
	      *invalid = 1; return 0;
	    }
	  }
	} else if (induced) {
	  /* (u, u2) is not an edge => remove neighbors of v from D[u2] */
	  if (VECTOR(D->nbVal)[u2] < VECTOR(Gt->nbSucc)[v]) {
	    j = VECTOR(D->firstVal)[u2]; 
	    while (j < VECTOR(D->firstVal)[u2] + VECTOR(D->nbVal)[u2]) {
	      if (!MATRIX(Gt->isEdge, v, VECTOR(D->val)[j])) {
		j++;
	      } else if (!igraph_i_lad_removeValue(u2, VECTOR(D->val)[j], D, 
						   Gp, Gt)) {
		*invalid = 1; return 0;
	      }
	    }
	  } else {
	    for (j=0; j<VECTOR(Gt->nbSucc)[v]; j++) {
	      if ((igraph_i_lad_isInD(u2, (int) VECTOR(*vneis)[j], D)) && 
		  (!igraph_i_lad_removeValue(u2, (int) VECTOR(*vneis)[j], D,
					     Gp, Gt))) {
		*invalid = 1; return 0; 
	      }
	    }
	  }
	}
	if (VECTOR(D->nbVal)[u2] == 0) {
	  *invalid = 1; /* D[u2] is empty */
	  return 0;
	}
	if ((VECTOR(D->nbVal)[u2] == 1) && (oldNbVal > 1)) { 
	  VECTOR(*toBeMatched)[nb++]=u2; 
	}
      }			
    }
  }
  *invalid = 0;
  return 0;
}


bool igraph_i_lad_matchVertex(int u, bool induced, Tdomain* D, Tgraph* Gp, 
			      Tgraph *Gt) {
  int invalid;
  /* match u to D->val[D->firstVal[u]] and filter domains of other non
     matched vertices wrt FC(Edges) and FC(diff) (this is not
     mandatory, as LAD is stronger than FC(Edges) and GAC(allDiff) 
     is stronger than FC(diff), but this speeds up the solution process).
     return false if an inconsistency is detected by FC(Edges) or
     FC(diff); true otherwise; */
  igraph_vector_int_t toBeMatched;
  igraph_vector_int_init(&toBeMatched, Gp->nbVertices);
  IGRAPH_FINALLY(igraph_vector_int_destroy, &toBeMatched);
  VECTOR(toBeMatched)[0]=u;
  igraph_i_lad_matchVertices(1, &toBeMatched, induced, D, Gp, Gt, 
			     &invalid);
  igraph_vector_int_destroy(&toBeMatched);
  IGRAPH_FINALLY_CLEAN(1);

  return invalid ? false : true;
}


int igraph_i_lad_qcompare (void const *a, void const *b) {
  /* function used by the qsort function */
  int pa = *((int*)a) - *((int*)b);
  return pa;
}

bool igraph_i_lad_compare(int size_mu, int* mu, int size_mv, int* mv) {
  /* return true if for every element u of mu there exists
     a different element v of mv such that u <= v; 
     return false otherwise */
  int i, j;
  qsort(mu, (size_t) size_mu, sizeof(int), igraph_i_lad_qcompare);
  qsort(mv, (size_t) size_mv, sizeof(int), igraph_i_lad_qcompare);
  i = size_mv-1;
  for (j=size_mu-1; j>=0; j--) {
    if (mu[j]>mv[i]) { return false; }
    i--;
  }
  return true;
}

int igraph_i_lad_initDomains(bool initialDomains, 
			     igraph_vector_ptr_t *domains, Tdomain* D,
			     Tgraph* Gp, Tgraph* Gt, int *empty) {
  /* for every pattern node u, initialize D(u) with every vertex v 
     such that for every neighbor u' of u there exists a different 
     neighbor v' of v such that degree(u) <= degree(v)
     if initialDomains, then filter initial domains wrt
     compatibilities given in file 
     return false if a domain is empty and true otherwise */
  int val[Gp->nbVertices*Gt->nbVertices];
  bool dom[Gt->nbVertices];
  int matchingSize, u, v, i, j;

  IGRAPH_CHECK(igraph_vector_int_init(&D->globalMatchingP, Gp->nbVertices));
  IGRAPH_FINALLY(igraph_vector_int_destroy, &D->globalMatchingP);
  igraph_vector_int_fill(&D->globalMatchingP, -1L);

  IGRAPH_CHECK(igraph_vector_int_init(&D->globalMatchingT, Gt->nbVertices));
  IGRAPH_FINALLY(igraph_vector_int_destroy, &D->globalMatchingT);
  igraph_vector_int_fill(&D->globalMatchingT, -1L);

  IGRAPH_CHECK(igraph_vector_int_init(&D->nbVal, Gp->nbVertices));
  IGRAPH_FINALLY(igraph_vector_int_destroy, &D->nbVal);

  IGRAPH_CHECK(igraph_vector_int_init(&D->firstVal, Gp->nbVertices));
  IGRAPH_FINALLY(igraph_vector_int_destroy, &D->firstVal);

  IGRAPH_CHECK(igraph_matrix_int_init(&D->posInVal, 
				      Gp->nbVertices, Gt->nbVertices));
  IGRAPH_FINALLY(igraph_matrix_int_destroy, &D->posInVal);

  IGRAPH_CHECK(igraph_matrix_int_init(&D->firstMatch,
				      Gp->nbVertices, Gt->nbVertices));
  IGRAPH_FINALLY(igraph_matrix_int_destroy, &D->firstMatch);

  IGRAPH_CHECK(igraph_vector_char_init(&D->markedToFilter, Gp->nbVertices));
  IGRAPH_FINALLY(igraph_vector_char_destroy, &D->markedToFilter);

  IGRAPH_CHECK(igraph_vector_int_init(&D->toFilter, Gp->nbVertices));
  IGRAPH_FINALLY(igraph_vector_int_destroy, &D->toFilter);

  D->valSize = 0;
  matchingSize = 0;
	
  for (u=0; u<Gp->nbVertices; u++) {
    igraph_vector_t *Gp_uneis=igraph_adjlist_get(&Gp->succ, u);
    if (initialDomains) { 
      /* read the list of target vertices which are compatible with u */
      igraph_vector_t *vec=VECTOR(*domains)[u];
      i=(int) igraph_vector_size(vec);
      memset(dom, false, sizeof(bool)*(size_t)(Gt->nbVertices));
      for (j=0; j<i; j++) {
	int v=(int) VECTOR(*vec)[j];
	dom[v] = true;
      }
    }
    VECTOR(D->markedToFilter)[u] = true;
    VECTOR(D->toFilter)[u] = u;
    VECTOR(D->nbVal)[u] = 0;
    VECTOR(D->firstVal)[u] = D->valSize;
    for (v=0; v<Gt->nbVertices; v++) {
      igraph_vector_t *Gt_vneis=igraph_adjlist_get(&Gt->succ, v);
      if ((initialDomains) && (!dom[v])) { /* v not in D(u) */
	MATRIX(D->posInVal, u, v) = (int) (VECTOR(D->firstVal)[u] + 
					   Gt->nbVertices);
      } else {
	MATRIX(D->firstMatch, u, v) = matchingSize;
	matchingSize += VECTOR(Gp->nbSucc)[u];
	if (VECTOR(Gp->nbSucc)[u] <= VECTOR(Gt->nbSucc)[v]) {
	  int mu[(long int) VECTOR(Gp->nbSucc)[u]], 
	    mv[(long int) VECTOR(Gt->nbSucc)[v]];
	  for (i=0; i<VECTOR(Gp->nbSucc)[u]; i++) { 
	    mu[i]=(int) VECTOR(Gp->nbSucc)[(long int) VECTOR(*Gp_uneis)[i]];
	  }
	  for (i=0; i<VECTOR(Gt->nbSucc)[v]; i++) { 
	    mv[i]=(int) VECTOR(Gt->nbSucc)[(long int) VECTOR(*Gt_vneis)[i]];
	  }
	  if (igraph_i_lad_compare((int) VECTOR(Gp->nbSucc)[u], mu, 
				   (int) VECTOR(Gt->nbSucc)[v], mv)==1) {
	    val[D->valSize] = v;
	    VECTOR(D->nbVal)[u]++;
	    MATRIX(D->posInVal, u, v) = D->valSize++;
	  } else {  /* v not in D(u) */
	    MATRIX(D->posInVal, u, v) = 
	      (int)(VECTOR(D->firstVal)[u] + Gt->nbVertices);
	  }
	} else {  /* v not in D(u) */
	  MATRIX(D->posInVal, u, v) = 
	    (int) (VECTOR(D->firstVal)[u] + Gt->nbVertices);
	}
      }
    }
    if (VECTOR(D->nbVal)[u] == 0) { 
      *empty = 1;  /* empty domain */
      return 0;
    }
  }
  IGRAPH_CHECK(igraph_vector_int_init(&D->val, D->valSize));
  IGRAPH_FINALLY(igraph_vector_int_destroy, &D->val);
  for (i=0; i<D->valSize; i++) { VECTOR(D->val)[i] = val[i]; }

  IGRAPH_CHECK(igraph_vector_int_init(&D->matching, matchingSize));
  IGRAPH_FINALLY(igraph_vector_int_destroy, &D->matching);
  igraph_vector_int_fill(&D->matching, -1);

  D->nextOutToFilter = 0;
  D->lastInToFilter = (int) (Gp->nbVertices-1);
  *empty=0;
  return 0;
}

/* ---------------------------------------------------------*/
/* Coming from allDiff.c                                    */
/* ---------------------------------------------------------*/

#define white 0
#define grey 1
#define black 2
#define toBeDeleted 3
#define deleted 4

void igraph_i_lad_addToDelete(int u, int* list, int* nb, int* marked) {
  if (marked[u]<toBeDeleted) {
    list[(*nb)++]=u;
    marked[u]=toBeDeleted; 
  }
}

int igraph_i_lad_updateMatching(int sizeOfU, int sizeOfV, 
				igraph_vector_int_t *degree, 
				igraph_vector_int_t *firstAdj, 
				igraph_vector_int_t *adj, 
				igraph_vector_int_t * matchedWithU, 
				int *invalid) {
  /* input:
     sizeOfU = number of vertices in U
     sizeOfV = number of vertices in V
     degree[u] = number of vertices of V which are adjacent to u
     firstAdj[u] = pos in adj of the first vertex of V adjacent to u
     adj[firstAdj[u]..firstAdj[u]+sizeOfU[u]-1] = vertices of V adjacent to u
     
     input/output:
     matchedWithU[u] = vertex of V matched with u
	
     returns true if there exists a matching that covers U, i.e., if
     for every u in 0..nbU-1, there exists a different v in 0..nb-1
     such that v is adjacent to u; returns false otherwise */
	
  if (sizeOfU>sizeOfV) {
    *invalid = 1; /* trivial case of infeasibility */
    return 0; 
  }
	
  int matchedWithV[sizeOfV]; /* matchedWithV[matchedWithU[u]]=u */
  int nbPred[sizeOfV]; /* nbPred[i] = nb of predecessors of the ith
			  vertex of V in the DAG */
  int pred[sizeOfV][sizeOfU]; /* pred[i][j] = jth predecessor the ith
				 vertex of V in the DAG */
  int nbSucc[sizeOfU]; /* nbSucc[i] = nb of successors of the ith
			  vertex of U in the DAG */
  int succ[sizeOfU][sizeOfV]; /* succ[i][j] = jth successor of the ith
				 vertex of U in the DAG */
  int listV[sizeOfV], listU[sizeOfU], listDV[sizeOfV], listDU[sizeOfU];
  int nbV, nbU, nbDV, nbDU;
  int i, j, k, stop, u, v, w;
  int markedV[sizeOfV], markedU[sizeOfU];
  /* markedX[i]=white if X[i] is not in the DAG
     markedX[i]=grey if X[i] has been added to the DAG, but not its successors
     markedX[i]=black if X[i] and its successors have been added to the DAG
     markedX[i]=toBeDeleted if X[i] must be deleted from the DAG
     markedX[i]=deleted if X[i] has been deleted from the DAG */
  int nbUnmatched = 0; /* number of vertices of U that are not matched */
  int unmatched[sizeOfU]; /* vertices of U that are not matched */
  int posInUnmatched[sizeOfU]; /* unmatched[posInUnmatched[u]]=u */
	
  /* initialize matchedWithV and unmatched */
  memset(matchedWithV, -1, (size_t)sizeOfV*sizeof(int));
  for (u=0; u<sizeOfU; u++) {
    if (VECTOR(*matchedWithU)[u] >= 0) {
      matchedWithV[VECTOR(*matchedWithU)[u]]=u;
    } else {
      posInUnmatched[u]=nbUnmatched;
      unmatched[nbUnmatched++]=u;
    }
  }
  /* try to match unmatched vertices of U with free vertices of V */
  j=0;
  while (j<nbUnmatched) {
    u = unmatched[j];
    for (i=VECTOR(*firstAdj)[u]; 
	 ((i < VECTOR(*firstAdj)[u] + VECTOR(*degree)[u]) && 
	  (matchedWithV[VECTOR(*adj)[i]] >= 0)); i++) { }
    if (i == VECTOR(*firstAdj)[u] + VECTOR(*degree)[u]) { 
      j++; /* no free vertex for u */
    } else {
      v=VECTOR(*adj)[i]; /* v is free => match u with v */
      VECTOR(*matchedWithU)[u]=v; 
      matchedWithV[v]=u; 
      unmatched[j]=unmatched[--nbUnmatched];
      posInUnmatched[unmatched[j]]=j;
    }
  }
	
  while (nbUnmatched > 0) { 
    /* Try to increase the number of matched vertices */
    /* step 1 : build the DAG */
    memset(markedU, white, (size_t) sizeOfU*sizeof(int));
    memset(nbSucc, 0, (size_t) sizeOfU*sizeof(int));
    memset(markedV, white, (size_t) sizeOfV*sizeof(int));
    memset(nbPred, 0, (size_t) sizeOfV*sizeof(int));
    /* first layer of the DAG from the free nodes of U */
    nbV=0;
    for (j=0; j<nbUnmatched; j++) {
      u=unmatched[j]; /* u is a free node of U */
      markedU[u]=black;
      for (i = VECTOR(*firstAdj)[u]; 
	   i < VECTOR(*firstAdj)[u] + VECTOR(*degree)[u]; i++) {
	v=VECTOR(*adj)[i]; /* add edge (u, v) to the DAG */
	pred[v][nbPred[v]++]=u;
	succ[u][nbSucc[u]++]=v;
	if (markedV[v]==white) { /* first time v is added to the DAG*/
	  markedV[v]=grey; 
	  listV[nbV++]=v;
	}
      } 
    }
    stop=0;
    while ((stop==0) && (nbV>0)) {
      /* build next layer from nodes of V to nodes of U */
      nbU=0;
      for (i=0; i<nbV; i++) {
	v=listV[i];
	markedV[v]=black;
	u=matchedWithV[v]; 
	if (markedU[u]==white) { /* edge (v, u) belongs to the DAG */
	  markedU[u]=grey; 
	  listU[nbU++]=u;
	}
      }
      /* build next layer from nodes of U to nodes of V */
      nbV=0;
      for (j=0; j<nbU; j++) {
	u=listU[j];
	markedU[u]=black;
	for (i = VECTOR(*firstAdj)[u]; 
	     i < VECTOR(*firstAdj)[u] + VECTOR(*degree)[u];i++) {
	  v=VECTOR(*adj)[i]; 
	  if (markedV[v]!=black) { /* add edge (u, v) to the DAG */
	    pred[v][nbPred[v]++]=u;
	    succ[u][nbSucc[u]++]=v;
	    if (markedV[v]==white) { /* first time v is added to the DAG */
	      markedV[v]=grey; 
	      listV[nbV++]=v;
	    }
	    if (matchedWithV[v]==-1) { /* we have found a free node ! */
	      stop=1; 
	    }
	  }
	}
      }
    }
    if (nbV==0) {
      *invalid = 1; 
      return 0;
    }
		
    /* step 2: look for augmenting paths */
    for (k=0; k<nbV; k++) { 
      v=listV[k];
      if ((matchedWithV[v]==-1) && (nbPred[v]>0)) {
	/* v is the final node of an augmenting path */
	int path[sizeOfU+sizeOfV];
	int length = 1;
	path[0]=v;
	nbDV=0; 
	nbDU=0;
	igraph_i_lad_addToDelete(v, listDV, &nbDV, markedV);
	do{
	  u=pred[v][0]; /* (u, v) belongs to the augmenting path */
	  path[length++]=u;
	  igraph_i_lad_addToDelete(u, listDU, &nbDU, markedU);
	  if (VECTOR(*matchedWithU)[u]!=-1) {
	    /* u is not the initial node of the augmenting path */
	    v=VECTOR(*matchedWithU)[u]; /* (v, u) belongs to the
					   augmenting path */
	    path[length++]=v;
	    igraph_i_lad_addToDelete(v, listDV, &nbDV, markedV);
	  }
	} while (VECTOR(*matchedWithU)[u]!=-1);
				
	/* delete nodes of listDV and listDU */
	while ((nbDV>0) || (nbDU>0)) {
	  while (nbDV>0) { /* delete v */
	    v=listDV[--nbDV]; markedV[v]=deleted;
	    u=matchedWithV[v];
	    if (u!=-1) { 
	      igraph_i_lad_addToDelete(u, listDU, &nbDU, markedU); 
	    }
	    for (i=0; i<nbPred[v]; i++) {
	      u=pred[v][i]; /* delete edge (u, v) */
	      for (j=0; ((j<nbSucc[u]) && (v!=succ[u][j])); j++) { }
	      succ[u][j]=succ[u][--nbSucc[u]];
	      if (nbSucc[u]==0) {
		igraph_i_lad_addToDelete(u, listDU, &nbDU, markedU);
	      }
	    }
	  }
	  while (nbDU>0) { /* delete u */
	    u = listDU[--nbDU]; markedU[u]=deleted;
	    v=VECTOR(*matchedWithU)[u];
	    if (v!=-1) { 
	      igraph_i_lad_addToDelete(v, listDV, &nbDV, markedV);
	    }
	    j=0;
	    for (i=0; i<nbSucc[u]; i++) { /* delete edge (u, v) */
	      v=succ[u][i];
	      for (j=0; ((j<nbPred[v]) && (u!=pred[v][j])); j++) { }
	      pred[v][j]=pred[v][--nbPred[v]];
	      if (nbPred[v]==0) {
		igraph_i_lad_addToDelete(v, listDV, &nbDV, markedV);
	      }
	    }
	  }
	}
	/* Remove the last node of the augmenting path from the set of
	   unmatched vertices */
	u=path[length-1]; 
	i=posInUnmatched[u];
	unmatched[i]=unmatched[--nbUnmatched];
	posInUnmatched[unmatched[i]]=i;
	/* Update the matching wrt the augmenting path */
	while (length>1) {
	  u=path[length-1]; v=path[length-2]; length-=2;
	  w=matchedWithV[v]; /* match v with u instead of v with w */
	  VECTOR(*matchedWithU)[u]=v; 
	  matchedWithV[v]=u;
	}
      }
    }
  }
  *invalid=0;
  return 0;
}

void igraph_i_lad_DFS(int nbU, int nbV, int u, bool* marked, int* nbSucc, 
		      int succ[nbV][nbU], igraph_vector_int_t * matchedWithU,
		      int* order, int* nb) {
  /* perform a depth first search, starting from u, in the bipartite
     graph Go=(U, V, E) such that 
     U = vertices of Gp
     V = vertices of Gt
     E = { (u, matchedWithU[u]) / u is a vertex of Gp } U
         { (v, u) / v is a vertex of D[u] which is not matched to v}

     Given a vertex v of Gt, nbSucc[v]=number of successors of v and
     succ[v]=list of successors of v. order[nb^out+1..nb^in] contains
     the vertices discovered by the DFS */
  int i;
  marked[u]=true;
  int v=VECTOR(*matchedWithU)[u]; /* the only one predecessor of v is u */
  for (i=0; i<nbSucc[v]; i++) {
    if (!marked[succ[v][i]]) {
      igraph_i_lad_DFS(nbU, nbV, succ[v][i], marked, nbSucc, succ, 
		       matchedWithU, order, nb);
    }
  }
  /* we have finished with u => number it */
  order[*nb]=u; (*nb)--;
}

void igraph_i_lad_SCC(int nbU, int nbV, int* numV, int* numU, 
		      int* nbSucc, int succ[nbV][nbU], 
		      int* nbPred, int pred[nbU][nbV], 
		      igraph_vector_int_t * matchedWithU,
		      igraph_vector_int_t * matchedWithV) {
  /* postrelation: numV[v]==numU[u] iff they belong to the same
     strongly connected component in the bipartite graph Go=(U, V, E)
     such that 
     U = vertices of Gp
     V = vertices of Gt
     E = { (u, matchedWithU[u]) / u is a vertex of Gp } U
         { (v, u) / v is a vertex of D[u] which is not matched to v}

     Given a vertex v of Gt, nbSucc[v]=number of sucessors of v and
     succ[v]=list of successors of v */
  int order[nbU];
  bool marked[nbU];
  int fifo[nbV];
  int u, v, i, j, k, nbSCC, nb;
	
  /* Order vertices of Gp wrt DFS */
  memset(marked, false, (size_t) nbU*sizeof(int));
  nb=nbU-1;
  for (u=0; u<nbU; u++) {
    if (!marked[u]) {
      igraph_i_lad_DFS(nbU, nbV, u, marked, nbSucc, succ, matchedWithU, 
		       order, &nb);
    }
  }
	
  /* traversal starting from order[0], then order[1], ... */
  nbSCC=0;
  memset(numU, -1, (size_t) nbU*sizeof(int));
  memset(numV, -1, (size_t) nbV*sizeof(int));
  for (i=0; i<nbU; i++) {
    u=order[i];
    v=VECTOR(*matchedWithU)[u];
    if (numV[v]==-1) { /* v belongs to a new SCC */
      nbSCC++;
      k=1; fifo[0]=v;
      numV[v]=nbSCC;
      while (k>0) {
	v=fifo[--k];
	u=VECTOR(*matchedWithV)[v];
	if (u!=-1) {
	  numU[u]=nbSCC;
	  for (j=0; j<nbPred[u]; j++) {
	    v=pred[u][j];
	    if (numV[v]==-1) {
	      numV[v]=nbSCC;
	      fifo[k++]=v;
	    }
	  }
	}
      }
    }
  }
}


int igraph_i_lad_ensureGACallDiff(bool induced, Tgraph* Gp, Tgraph* Gt, 
				  Tdomain* D, int *invalid) {
  /* precondition: D->globalMatchingP is an all different matching of
     the pattern vertices 
     postcondition: filter domains wrt GAC(allDiff)
     return false if an inconsistency is detected; true otherwise
	
     Build the bipartite directed graph Go=(U, V, E) such that
     E = { (u, v) / u is a vertex of Gp which is matched to v (i.e.,
            v=D->globalMatchingP[u])} U 
         { (v, u) / v is a vertex of Gt which is in D(u) but is not
	   matched to u} */
  int nbPred[Gp->nbVertices]; /* nbPred[u] = nb of predecessors of u in Go */
  int pred[Gp->nbVertices][Gt->nbVertices]; /* pred[u][i] = ith
					       predecessor of u in Go */
  int nbSucc[Gt->nbVertices]; /* nbSucc[v] = nb of successors of v in Go */
  int succ[Gt->nbVertices][Gp->nbVertices]; /* succ[v][i] = ith
					       successor of v in Go */
  int u, v, i, w, oldNbVal, nbToMatch;  
  int numV[Gt->nbVertices], numU[Gp->nbVertices];
  igraph_vector_int_t toMatch;
  bool used[Gp->nbVertices][Gt->nbVertices];
  IGRAPH_CHECK(igraph_vector_int_init(&toMatch, Gp->nbVertices));
  IGRAPH_FINALLY(igraph_vector_int_destroy, &toMatch);
  memset(numV, false, (size_t) (Gt->nbVertices)*sizeof(int));
  memset(nbSucc, 0, (size_t) (Gt->nbVertices)*sizeof(int));
  memset(nbPred, 0, (size_t) (Gp->nbVertices)*sizeof(int));
  memset(numU, false, (size_t)(Gp->nbVertices)*sizeof(int));
  for (u=0; u<Gp->nbVertices; u++) {
    for (i=0; i < VECTOR(D->nbVal)[u]; i++) {
      v=VECTOR(D->val)[ VECTOR(D->firstVal)[u]+i ]; /* v in D(u) */
      used[u][v]=false;
      if (v != VECTOR(D->globalMatchingP)[u]) {
	pred[u][nbPred[u]++]=v; 
	succ[v][nbSucc[v]++]=u; 
      }
    }
  }
	
  /* mark as used all edges of paths starting from free vertices */
  int list[Gt->nbVertices];
  int nb=0;
  for (v=0; v<Gt->nbVertices; v++) {
    if (VECTOR(D->globalMatchingT)[v] < 0) { /* v is free */
      list[nb++]=v;
      numV[v]=true;
    }
  }
  while (nb>0) {
    v=list[--nb];
    for (i=0; i<nbSucc[v]; i++) {
      u=succ[v][i];
      used[u][v]=true;
      if (numU[u]==false) {
	numU[u]=true;
	w=VECTOR(D->globalMatchingP)[u];
	used[u][w]=true;
	if (numV[w]==false) {
	  list[nb++]=w;
	  numV[w]=true;
	}
      }
    }
  }
	
  /* look for strongly connected components in Go */
  igraph_i_lad_SCC((int)(Gp->nbVertices), (int)(Gt->nbVertices), numV, numU, nbSucc, succ, 
		   nbPred, pred, &D->globalMatchingP, &D->globalMatchingT);
	
  /* remove v from D[u] if (u, v) is not marked as used 
                        and u and v are not in the same SCC 
                        and D->globalMatchingP[u] != v */
  nbToMatch = 0;
  for (u=0; u<Gp->nbVertices; u++) {
    oldNbVal = VECTOR(D->nbVal)[u];
    for (i=0; i < VECTOR(D->nbVal)[u]; i++) {
      v=VECTOR(D->val)[ VECTOR(D->firstVal)[u]+i ]; /* v in D(u) */
      if ((!used[u][v]) && (numV[v]!=numU[u]) &&
	  (VECTOR(D->globalMatchingP)[u]!=v)) {
	if (!igraph_i_lad_removeValue(u, v, D, Gp, Gt)) {
	  *invalid = 1;
	  return 0;
	}
      }
    }
    if (VECTOR(D->nbVal)[u] == 0) {
      *invalid = 1; 
      return 0;
    }
    if ((oldNbVal>1) && (VECTOR(D->nbVal)[u]==1)) {
      VECTOR(toMatch)[nbToMatch++] = u; 
    }
  }
  IGRAPH_CHECK(igraph_i_lad_matchVertices(nbToMatch, &toMatch, induced, 
					  D, Gp, Gt, invalid));
  
  igraph_vector_int_destroy(&toMatch);
  IGRAPH_FINALLY_CLEAN(1);
  
  return 0;
}

/* ---------------------------------------------------------*/
/* Coming from lad.c                                        */
/* ---------------------------------------------------------*/

bool igraph_i_lad_checkLAD(int u, int v, Tdomain* D, Tgraph* Gp, Tgraph* Gt) {
  /* return true if G_(u, v) has a adj(u)-covering matching; false
     otherwise */
  int u2, v2, i, j;
  int nbMatched = 0;
  igraph_vector_t *Gp_uneis=igraph_adjlist_get(&Gp->succ, u);
	
  /* special case when u has only 1 adjacent node => no need to call
     Hopcroft and Karp */
  if (VECTOR(Gp->nbSucc)[u]==1) {
    u2 = (int) VECTOR(*Gp_uneis)[0]; /* u2 is the only node adjacent to u */
    v2 = VECTOR(D->matching)[ MATRIX(D->firstMatch, u, v) ];
    if ((v2 != -1) && (igraph_i_lad_isInD(u2, v2, D))) { return true; }
    /* look for a support of edge (u, u2) for v */
    for (i=VECTOR(D->firstVal)[u2]; 
	 i < VECTOR(D->firstVal)[u2] + VECTOR(D->nbVal)[u2]; i++) {
      if (MATRIX(Gt->isEdge, v, VECTOR(D->val)[i])) {
	VECTOR(D->matching)[ MATRIX(D->firstMatch, u, v) ] = 
	  VECTOR(D->val)[i];
	return true;
      }
    }
    return false;
  }
	
  /* general case (when u has more than 1 adjacent node) */
  for (i=0; i<VECTOR(Gp->nbSucc)[u]; i++) { 
    /* remove from the matching of G_(u, v) edges which no longer
       belong to G_(u, v) */
    u2 = (int) VECTOR(*Gp_uneis)[i];
    v2 = VECTOR(D->matching)[ MATRIX(D->firstMatch, u, v)+i];
    if ((v2 != -1) && (igraph_i_lad_isInD(u2, v2, D))) { nbMatched++; }
  }
  if (nbMatched == VECTOR(Gp->nbSucc)[u]) { 
    return true; 
  } /* The matching still covers adj(u) */
	
  /* Build the bipartite graph
     let U be the set of nodes adjacent to u
     let V be the set of nodes that are adjacent to v, and that belong
     to domains of nodes of U */
  int num[Gt->nbVertices], numInv[Gt->nbVertices];
  /* nbComp[u]=number of elements of V that are compatible with u */
  igraph_vector_int_t nbComp;
  igraph_vector_int_init(&nbComp, (long int) VECTOR(Gp->nbSucc)[u]);
  IGRAPH_FINALLY(igraph_vector_int_destroy, &nbComp);
  igraph_vector_int_t firstComp;
  igraph_vector_int_init(&firstComp, (long int) VECTOR(Gp->nbSucc)[u]);
  IGRAPH_FINALLY(igraph_vector_int_destroy, &firstComp);
  /* comp[firstComp[u]..firstComp[u]+nbComp[u]-1] = nodes of Gt that
     are compatible with u */
  igraph_vector_int_t comp;
  igraph_vector_int_init(&comp, (long int) (VECTOR(Gp->nbSucc)[u] * 
					    Gt->nbVertices));
  IGRAPH_FINALLY(igraph_vector_int_destroy, &comp);
  int nbNum=0;
  int posInComp=0;
  igraph_vector_int_t matchedWithU;
  igraph_vector_int_init(&matchedWithU, (long int) VECTOR(Gp->nbSucc)[u]);
  IGRAPH_FINALLY(igraph_vector_int_destroy, &matchedWithU);
  memset(num, -1, (size_t) (Gt->nbVertices) * sizeof(int));
  for (i=0; i<VECTOR(Gp->nbSucc)[u]; i++) {
    u2 = (int) VECTOR(*Gp_uneis)[i]; /* u2 is adjacent to u */
    /* search for all nodes v2 in D[u2] which are adjacent to v */
    VECTOR(nbComp)[i]=0;
    VECTOR(firstComp)[i]=posInComp;
    if (VECTOR(D->nbVal)[u2] > VECTOR(Gt->nbSucc)[v]) {
      for (j=VECTOR(D->firstVal)[u2]; 
	   j < VECTOR(D->firstVal)[u2] + VECTOR(D->nbVal)[u2]; j++) {
	v2 = VECTOR(D->val)[j]; /* v2 belongs to D[u2] */
	if (MATRIX(Gt->isEdge, v, v2)) { /* v2 is a successor of v */
	  if (num[v2]<0) { /* v2 has not yet been added to V */
	    num[v2]=nbNum;
	    numInv[nbNum++]=v2;
	  } 
	  VECTOR(comp)[posInComp++]=num[v2];
	  VECTOR(nbComp)[i]++;
	}
      }
    } else {
      igraph_vector_t *Gt_vneis=igraph_adjlist_get(&Gt->succ, v);
      for (j=0; j<VECTOR(Gt->nbSucc)[v]; j++) {
	v2 = (int) VECTOR(*Gt_vneis)[j]; /* v2 is a successor of v */
	if (igraph_i_lad_isInD(u2, v2, D)) { /* v2 belongs to D[u2] */
	  if (num[v2]<0) { /* v2 has not yet been added to V */
	    num[v2]=nbNum;
	    numInv[nbNum++]=v2;
	  } 
	  VECTOR(comp)[posInComp++]=num[v2];
	  VECTOR(nbComp)[i]++;
	}
      }			
    }
    if (VECTOR(nbComp)[i]==0) { 
      igraph_vector_int_destroy(&matchedWithU);
      igraph_vector_int_destroy(&comp);
      igraph_vector_int_destroy(&firstComp);
      igraph_vector_int_destroy(&nbComp);
      IGRAPH_FINALLY_CLEAN(4);
      return false; /* u2 has no compatible vertex in succ[v] */
    }
    /* u2 is matched to v2 in the matching that supports (u, v) */
    v2 = VECTOR(D->matching)[ MATRIX(D->firstMatch, u, v)+i]; 
    if ((v2 != -1) && (igraph_i_lad_isInD(u2, v2, D))) {
      VECTOR(matchedWithU)[i]=num[v2];
    } else { 
      VECTOR(matchedWithU)[i]=-1;
    }
  }
  /* Call Hopcroft Karp to update the matching */
  int invalid;
  igraph_i_lad_updateMatching((int) VECTOR(Gp->nbSucc)[u], nbNum, &nbComp, 
			      &firstComp, &comp, &matchedWithU, &invalid);
  if (invalid) { 
    igraph_vector_int_destroy(&matchedWithU);
    igraph_vector_int_destroy(&comp);
    igraph_vector_int_destroy(&firstComp);
    igraph_vector_int_destroy(&nbComp);
    IGRAPH_FINALLY_CLEAN(4);
    return false; 
  }
  for (i=0; i<VECTOR(Gp->nbSucc)[u]; i++) {
    VECTOR(D->matching)[ MATRIX(D->firstMatch, u, v)+i] =
      numInv[ VECTOR(matchedWithU)[i] ];
  }
  igraph_vector_int_destroy(&matchedWithU);
  igraph_vector_int_destroy(&comp);
  igraph_vector_int_destroy(&firstComp);
  igraph_vector_int_destroy(&nbComp);
  IGRAPH_FINALLY_CLEAN(4);
  return true;
}

/* ---------------------------------------------------------*/
/* Coming from main.c                                      */
/* ---------------------------------------------------------*/

bool igraph_i_lad_filter(bool induced, Tdomain* D, Tgraph* Gp, Tgraph* Gt) {
  /* filter domains of all vertices in D->toFilter wrt LAD and ensure
     GAC(allDiff) 
     return false if some domain becomes empty; true otherwise */
  int u, v, i, oldNbVal;
  while (!igraph_i_lad_toFilterEmpty(D)) {
    while (!igraph_i_lad_toFilterEmpty(D)) {
      u=igraph_i_lad_nextToFilter(D, (int) (Gp->nbVertices));
      oldNbVal = VECTOR(D->nbVal)[u];
      i = VECTOR(D->firstVal)[u]; 
      while (i < VECTOR(D->firstVal)[u] + VECTOR(D->nbVal)[u]) {
	/* for every target node v in D(u), check if G_(u, v) has a
	   covering matching */
	v=VECTOR(D->val)[i]; 
	if (igraph_i_lad_checkLAD(u, v, D, Gp, Gt)) {
	  i++;
	} else { 
	  if (!igraph_i_lad_removeValue(u, v, D, Gp, Gt)) { return false; }
	}
      }
      if ((VECTOR(D->nbVal)[u]==1) && (oldNbVal>1) && 
	  (!igraph_i_lad_matchVertex(u, induced, D, Gp, Gt))) { 
	return false; 
      }
      if (VECTOR(D->nbVal)[u]==0) { return false; }
    }
    int invalid;
    igraph_i_lad_ensureGACallDiff(induced, Gp, Gt, D, &invalid);
    if (invalid) { return false; }
  }
  return true;
}



int igraph_i_lad_solve(int timeLimit, bool firstSol, bool induced, 
		       Tdomain* D, Tgraph* Gp, Tgraph* Gt, 
		       int *invalid, igraph_bool_t *iso, 
		       igraph_vector_t *map, igraph_vector_ptr_t *maps, 
		       int *nbNodes, int *nbFail, int *nbSol, 
		       clock_t *begin) {
  /* if firstSol then search for the first solution; otherwise search
     for all solutions if induced then search for induced subgraphs;
     otherwise search for partial subgraphs 
     return false if CPU time limit exceeded before the search is
     completed, return true otherwise */
	
  int u, v, minDom, i; 
  int nbVal[Gp->nbVertices];
  int globalMatching[Gp->nbVertices];
  clock_t end=clock();

  (*nbNodes)++;

  if ( (double)(end - *begin) / CLOCKS_PER_SEC >= timeLimit) {
    /* CPU time limit exceeded */
    IGRAPH_ERROR("LAD CPU time exceeded", IGRAPH_CPUTIME);
  }
	
  if (!igraph_i_lad_filter(induced, D, Gp, Gt)) { 
    /* filtering has detected an inconsistency */
    (*nbFail)++;
    igraph_i_lad_resetToFilter(D);
    *invalid=0;
    return 0;
  }	
	
  /* The current node of the search tree is consistent wrt to LAD and
     GAC(allDiff) Save domain sizes and global all different matching 
     and search for the non matched vertex minDom with smallest domain */
  minDom=-1;
  for (u=0; u<Gp->nbVertices; u++) {
    nbVal[u]=VECTOR(D->nbVal)[u];
    if ((nbVal[u]>1) && ((minDom<0) || (nbVal[u]<nbVal[minDom]))) { 
      minDom=u; 
    }
    globalMatching[u] = VECTOR(D->globalMatchingP)[u];
  }
	
  if (minDom==-1) { 
    /* All vertices are matched => Solution found */
    if (iso) { *iso = 1; }
    (*nbSol)++;
    if (map && igraph_vector_size(map)==0) {
      IGRAPH_CHECK(igraph_vector_resize(map, Gp->nbVertices));
      for (u=0; u<Gp->nbVertices; u++) {
	VECTOR(*map)[u] = VECTOR(D->val)[ VECTOR(D->firstVal)[u] ];
      }
    }
    if (maps) {
      igraph_vector_t *vec=igraph_Calloc(1, igraph_vector_t);
      if (!vec) { IGRAPH_ERROR("LAD failed", IGRAPH_ENOMEM); }
      IGRAPH_FINALLY(igraph_free, vec);
      IGRAPH_CHECK(igraph_vector_init(vec, Gp->nbVertices));
      IGRAPH_FINALLY(igraph_vector_destroy, vec);
      for (u=0; u<Gp->nbVertices; u++) {
	VECTOR(*vec)[u] = VECTOR(D->val)[ VECTOR(D->firstVal)[u] ];
      }
      IGRAPH_CHECK(igraph_vector_ptr_push_back(maps, vec));
      IGRAPH_FINALLY_CLEAN(2);
    }
    igraph_i_lad_resetToFilter(D);
    *invalid=0;
    return 0;
  }
	
  /* save the domain of minDom to iterate on its values */
  int val[ VECTOR(D->nbVal)[minDom] ];
  for (i=0; i < VECTOR(D->nbVal)[minDom]; i++) {
    val[i]=VECTOR(D->val)[ VECTOR(D->firstVal)[minDom]+i ]; 
  }
	
  /* branch on minDom=v, for every target node v in D(u) */
  for(i=0; ((i<nbVal[minDom]) && ((firstSol==0)||(*nbSol==0))); i++) {
    IGRAPH_ALLOW_INTERRUPTION();
    v = val[i]; 
    if ((!igraph_i_lad_removeAllValuesButOne(minDom, v, D, Gp, Gt)) || 
	(!igraph_i_lad_matchVertex(minDom, induced, D, Gp, Gt))) {
      (*nbFail)++; 
      (*nbNodes)++;
      igraph_i_lad_resetToFilter(D);
    } else {
      int invalid;
      IGRAPH_CHECK(igraph_i_lad_solve(timeLimit, firstSol, induced,
				      D, Gp, Gt, &invalid, iso, map, maps, 
				      nbNodes, nbFail, nbSol, begin));
    }
    /* restore domain sizes and global all different matching */
    igraph_vector_int_fill(&D->globalMatchingT, -1);
    for (u=0; u<Gp->nbVertices; u++) {
      VECTOR(D->nbVal)[u] = nbVal[u];
      VECTOR(D->globalMatchingP)[u] = globalMatching[u];
      VECTOR(D->globalMatchingT)[globalMatching[u]] = u;
    }
  }
  *invalid=0;
  return 0;
}

/**
 * \function igraph_subisomorphic_lad
 * Check subgraph isomorphism with the LAD algorithm
 *
 * Check whether \p pattern is isomorphic to a subgraph os \p target.
 * The original LAD implementation by Christine Solnon was used as the 
 * basis of this code. 
 * 
 * </para><para>
 * See more about at http://liris.cnrs.fr/csolnon/LAD.html and in
 * Christine Solnon: AllDifferent-based Filtering for Subgraph
 * Isomorphism. Artificial Intelligence, 174(12-13):850-864, August
 * 2010, Elsevier
 * 
 * \param pattern The smaller graph, it can be directed or undirected.
 * \param target The bigger graph, it can be directed or undirected. 
 * \param domains A pointer vector, or a null pointer. If a pointer
 *    vector, then it must contain pointers to \c igraph_vector_t
 *    objects and the length of the vector must match the number of
 *    vertices in the \p pattern graph. For each vertex, the ids of
 *    the compatible vertices in the target graph are listed.
 * \param iso Pointer to a boolean, or a null pointer. If not a null
 *    pointer, then the boolean is set to TRUE (1) if a subgraph
 *    isomorphism is found, and to FALSE (0) otherwise.
 * \param map Pointer to a vector or a null pointer. If not a null
 *    pointer and a subgraph isomorphism is found, the matching
 *    vertices from the target graph are listed here, for each vertex
 *    (in vertex id order) from the pattern graph.
 * \param maps Pointer vector or a null pointer. If not a null
 *    pointer, then all subgraph isomorphisms are stored in the
 *    pointer vector, in \c igraph_vector_t objects.
 * \param induced Boolean, whether to search for induced matching
 *    subgraphs.
 * \param time_limit Processor time limit in seconds. Supply zero
 *    here for no limit. If the time limit is over, then the function
 *    signals an error.
 * \return Error code
 * 
 * \sa \ref igraph_subisomorphic_vf2() for the VF2 algorithm.
 * 
 * Time complexity: exponential.
 * 
 * \example examples/simple/igraph_subisomorphic_lad.c
 */

int igraph_subisomorphic_lad(const igraph_t *pattern, const igraph_t *target, 
			     igraph_vector_ptr_t *domains,
			     igraph_bool_t *iso, igraph_vector_t *map, 
			     igraph_vector_ptr_t *maps, 
			     igraph_bool_t induced, int time_limit) {

  bool firstSol = maps == 0;
  bool initialDomains = domains != 0;
  Tgraph Gp, Gt;
  Tdomain D;
  int invalidDomain;
  int u, nbToMatch = 0;
  igraph_vector_int_t toMatch;
  /* Number of nodes in the search tree */
  int nbNodes=0;
  /* number of failed nodes in the search tree */
  int nbFail=0;
  /* number of solutions found */
  int nbSol=0;
  /* reusable structure to get CPU time usage */
  clock_t begin=clock();

  if (!iso && !map && !maps) {
    IGRAPH_ERROR("Please give least one of `iso', `map' or `maps'", 
		 IGRAPH_EINVAL);
  }

  if (time_limit<=0) { time_limit = INT_MAX; }
    
  igraph_i_lad_createGraph(pattern, &Gp);
  igraph_i_lad_createGraph(target, &Gt);
  
  if (iso)  { *iso = 0; }
  if (map)  { igraph_vector_clear(map); } 
  if (maps) { igraph_vector_ptr_clear(maps); }

  if (Gp.nbVertices > Gt.nbVertices) { goto exit3; }
  
  IGRAPH_CHECK(igraph_i_lad_initDomains(initialDomains, domains, &D, &Gp, 
					&Gt, &invalidDomain));
  if (invalidDomain) { goto exit2; }
  
  IGRAPH_CHECK(igraph_i_lad_updateMatching((int) (Gp.nbVertices), 
					   (int) (Gt.nbVertices),
					   &D.nbVal, &D.firstVal, &D.val, 
					   &D.globalMatchingP, 
					   &invalidDomain));
  if (invalidDomain) { goto exit; }

  IGRAPH_CHECK(igraph_i_lad_ensureGACallDiff((char) induced, &Gp, &Gt, &D, 
					     &invalidDomain));
  if (invalidDomain) { goto exit; }

  for (u=0; u<Gp.nbVertices; u++) {
    VECTOR(D.globalMatchingT)[ VECTOR(D.globalMatchingP)[u] ] = u;
  }

  IGRAPH_CHECK(igraph_vector_int_init(&toMatch, Gp.nbVertices));
  IGRAPH_FINALLY(igraph_vector_int_destroy, &toMatch);
  
  for (u=0; u<Gp.nbVertices; u++) {
    if (VECTOR(D.nbVal)[u] == 1) { VECTOR(toMatch)[nbToMatch++] = u; }
  }
  IGRAPH_CHECK(igraph_i_lad_matchVertices(nbToMatch, &toMatch, (char) induced, 
					  &D, &Gp, &Gt, &invalidDomain));
  igraph_vector_int_destroy(&toMatch);
  IGRAPH_FINALLY_CLEAN(1);
  if (invalidDomain) { goto exit; }
	
  IGRAPH_CHECK(igraph_i_lad_solve(time_limit, firstSol, (char) induced, &D, 
				  &Gp, &Gt, &invalidDomain, iso, map, maps, 
				  &nbNodes, &nbFail, &nbSol, &begin));

 exit:
  
  igraph_vector_int_destroy(&D.val);
  igraph_vector_int_destroy(&D.matching);
  IGRAPH_FINALLY_CLEAN(2);

 exit2:
  
  igraph_vector_int_destroy(&D.globalMatchingP);
  igraph_vector_int_destroy(&D.globalMatchingT);
  igraph_vector_int_destroy(&D.nbVal);
  igraph_vector_int_destroy(&D.firstVal);
  igraph_matrix_int_destroy(&D.posInVal);
  igraph_matrix_int_destroy(&D.firstMatch);
  igraph_vector_char_destroy(&D.markedToFilter);
  igraph_vector_int_destroy(&D.toFilter);
  IGRAPH_FINALLY_CLEAN(8);

 exit3:

  igraph_matrix_char_destroy(&Gt.isEdge);
  igraph_adjlist_destroy(&Gt.succ);
  igraph_vector_destroy(&Gt.nbSucc);
  igraph_matrix_char_destroy(&Gp.isEdge);
  igraph_adjlist_destroy(&Gp.succ);
  igraph_vector_destroy(&Gp.nbSucc);
  IGRAPH_FINALLY_CLEAN(6);

  return 0;
}
