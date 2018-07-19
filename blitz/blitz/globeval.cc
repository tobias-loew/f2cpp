/***************************************************************************
 * blitz/array/eval.cc  Evaluate expression and assign to an array.
 *
 * $Id$
 *
 * Copyright (C) 1997-2011 Todd Veldhuizen <tveldhui@acm.org>
 * Copyright (C) 2017-2018 Tobias Loew <tobi@die-loews.de>
 *
 * This file is a part of Blitz.
 *
 * Blitz is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * Blitz is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public 
 * License along with Blitz.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * Suggestions:          tobi@die-loews.de
 * Bugs:                 tobi@die-loews.de    
 *
 * For more information, please see the Blitz++ Home Page:
 *    https://sourceforge.net/projects/blitz/
 *
 ****************************************************************************/
#ifndef BZ_GLOBEVAL_CC
#define BZ_GLOBEVAL_CC

#include <blitz/ranks.h>
#include <blitz/tvevaluate.h>
#include <blitz/blitz.h>

BZ_NAMESPACE(blitz)


// Fast traversals require <set> from the ISO/ANSI C++ standard library
#ifdef BZ_HAVE_STD
#ifdef BZ_ARRAY_SPACE_FILLING_TRAVERSAL


/** _bz_tryFastTraversal is a helper class.  Fast traversals are only
    attempted if the expression looks like a stencil -- it's at least
    three-dimensional, has at least six array operands, and there are
    no index placeholders in the expression.  These are all things
    which can be checked at compile time, so the if()/else() syntax
    has been replaced with this class template.  
*/
template<bool canTryFastTraversal>
struct _bz_tryFastTraversal {
    template<typename T_numtype, int N_rank, typename T_expr, typename T_update>
    static bool tryFast(Array<T_numtype,N_rank>& array, 
        T_expr expr, T_update)
    {
        return false;
    }
};

template<>
struct _bz_tryFastTraversal<true> {
    template<typename T_numtype, int N_rank, typename T_expr, typename T_update>
    static bool tryFast(Array<T_numtype,N_rank>& array, 
        T_expr expr, T_update)
    {
        // See if there's an appropriate space filling curve available.
        // Currently fast traversals use an N-1 dimensional curve.  The
        // Nth dimension column corresponding to each point on the curve
        // is traversed in the normal fashion.
        TraversalOrderCollection<N_rank-1> traversals;
        TinyVector<int, N_rank - 1> traversalGridSize;

        for (int i=0; i < N_rank - 1; ++i)
            traversalGridSize[i] = array.length(array.ordering(i+1));

#ifdef BZ_DEBUG_TRAVERSE
cout << "traversalGridSize = " << traversalGridSize << endl;
cout.flush();
#endif

        const TraversalOrder<N_rank-1>* order =
            traversals.find(traversalGridSize);

        if (order)
        {
#ifdef BZ_DEBUG_TRAVERSE
    cerr << "Array<" << BZ_DEBUG_TEMPLATE_AS_STRING_LITERAL(T_numtype)
         << ", " << N_rank << ">: Using stack traversal" << endl;
#endif
            // A curve was available -- use fast traversal.
            array.evaluateWithFastTraversal(*order, expr, T_update());
            return true;
        }

        return false;
    }
};
#endif // BZ_ARRAY_SPACE_FILLING_TRAVERSAL
#endif // BZ_HAVE_STD


/** Helper class that implements the evaluation routines for different
    ranks. */
template<int N, typename DestPolicy> struct _bz_evaluator {
  template<typename T_dest, typename T_expr, typename T_update>
  static void evaluateWithStackTraversal(T_dest&, T_expr, T_update);
  template<typename T_dest, typename T_numtype, typename T_update>
  static void evaluateValueWithStackTraversal(T_dest&, const T_numtype&, T_update);
  template<typename T_dest, typename T_expr, typename T_update>
  static void evaluateWithIndexTraversal(T_dest&, T_expr, T_update);
};
template<typename DestPolicy> struct _bz_evaluator<1, DestPolicy> {
    template<typename T_dest, typename T_expr, typename T_update>
    static void evaluateWithStackTraversal(T_dest&, T_expr, T_update);
    template<typename T_dest, typename T_numtype, typename T_update>
    static void evaluateValueWithStackTraversal(T_dest&, const T_numtype&, T_update);
    template<typename T_dest, typename T_expr, typename T_update>
    static void evaluateWithIndexTraversal(T_dest&, T_expr, T_update);
};
template<int N_base> struct _bz_evaluator<1, BZ_BLITZ_SCOPE(array_policy::StaticFortranArrayPolicy<N_base>)> {
    template<typename T_dest, typename T_expr, typename T_update>
    static void evaluateWithStackTraversal(T_dest&, T_expr, T_update);
    template<typename T_dest, typename T_numtype, typename T_update>
    static void evaluateValueWithStackTraversal(T_dest&, const T_numtype&, T_update);
    template<typename T_dest, typename T_expr, typename T_update>
    static void evaluateWithIndexTraversal(T_dest&, T_expr, T_update);
};

/**
  Assign an expression to a container.  For performance reasons, this
  function forwards to functions implementing one of several traversal
  mechanisms:
 
  - Index traversal scans through the destination array in storage order.
    The expression is evaluated using a TinyVector<int,N> operand.  This
    version is used only when there are index placeholders in the expression
    (see <blitz/indexexpr.h>)
  - Stack traversal also scans through the destination array in storage
    order.  However, push/pop stack iterators are used.
  - Fast traversal follows a Hilbert (or other) space-filling curve to
    improve cache reuse for stencilling operations.  Currently, the
    space filling curves must be generated by calling 
    generateFastTraversalOrder(TinyVector<int,N_dimensions>).
  - 2D tiled traversal follows a tiled traversal, to improve cache reuse
    for 2D stencils.  Space filling curves have too much overhead to use
    in two-dimensions. 
 */
template<typename T_dest, typename T_expr, typename T_update>
_bz_forceinline void
_bz_evaluate(T_dest& dest, T_expr expr, T_update)
{
  typedef typename T_dest::T_numtype T_numtype;
  const int N_rank = T_dest::rank_;

    // Check that all arrays have the same shape
#ifdef BZ_DEBUG
    if (!expr.shapeCheck(dest.shape()))
    {
      if (assertFailMode == false)
      {
        cerr << "[Blitz++] Shape check failed: Module " << __FILE__
             << " line " << __LINE__ << endl
             << "          Expression: ";
        prettyPrintFormat format(true);   // Use terse formatting
        BZ_STD_SCOPE(string) str;
        expr.prettyPrint(str, format);
        cerr << str << endl ;
      }

#if 0
// Shape dumping is broken by change to using string for prettyPrint
             << "          Shapes: " << shape() << " = ";
        prettyPrintFormat format2;
        format2.setDumpArrayShapesMode();
        expr.prettyPrint(cerr, format2);
        cerr << endl;
#endif
        BZ_PRE_FAIL;
    }
#endif

    BZPRECHECK(expr.shapeCheck(dest.shape()),
        "Shape check failed." << endl << "Expression:");

    BZPRECHECK((T_expr::rank_ == T_dest::rank_) || 
	       (T_expr::numArrayOperands == 0), 
	       "Assigned rank " << T_expr::rank_ << " expression to rank " 
	       << T_dest::rank_ << " array.");

    /*
     * Check that the arrays are not empty (e.g. length 0 arrays)
     * This fixes a bug found by Peter Bienstman, 6/16/99, where
     * Array<double,2> A(0,0),B(0,0); B=A(tensor::j,tensor::i);
     * went into an infinite loop.
     */

    const sizeType n = dest.numElements();
    if (n == 0) {
#ifdef BZ_DEBUG_TRAVERSE
      BZ_DEBUG_MESSAGE("Evaluating empty array, nothing to do");
#endif
      return;
    }
    // \todo this does not alvays compile, so eliminate for now.
    // if (n == 1) {
    //   // shortcut here since it's easy
    //   T_update::update(*dest.dataFirst(), expr(expr.lbound()));
    //   return;
    // }

#ifdef BZ_DEBUG_TRAVERSE
    BZ_DEBUG_MESSAGE( "T_expr::numIndexPlaceholders = " << T_expr::numIndexPlaceholders);
#endif

    // Tau profiling code.  Provide Tau with a pretty-printed version of
    // the expression.
    // NEEDS_WORK-- use a static initializer somehow.

#ifdef BZ_TAU_PROFILING
    static BZ_STD_SCOPE(string) exprDescription;
    if (!exprDescription.length())   // faked static initializer
    {
        exprDescription = "A";
        prettyPrintFormat format(true);   // Terse mode on
        format.nextArrayOperandSymbol();
        T_update::prettyPrint(exprDescription);
        expr.prettyPrint(exprDescription, format);
    }
    TAU_PROFILE(" ", exprDescription, TAU_BLITZ);
#endif

    // Determine which evaluation mechanism to use 
    if (T_expr::numIndexPlaceholders > 0)
    {
        // The expression involves index placeholders, so have to
        // use index traversal rather than stack traversal.

      _bz_evaluator<T_dest::rank_, void /* ignore Policy */>::evaluateWithIndexTraversal(dest, expr, T_update());
      return;
    }
    else {

        // If this expression looks like an array stencil, then attempt to
        // use a fast traversal order.
        // Fast traversals require <set> from the ISO/ANSI C++ standard
        // library.

#ifdef BZ_HAVE_STD
#ifdef BZ_ARRAY_SPACE_FILLING_TRAVERSAL

        enum { isStencil = (N_rank >= 3) && (T_expr::numArrayOperands > 6)
            && (T_expr::numIndexPlaceholders == 0) };

        if (_bz_tryFastTraversal<isStencil>::tryFast(dest, expr, T_update()))
            return;

#endif
#endif

#ifdef BZ_ARRAY_2D_STENCIL_TILING
        // Does this look like a 2-dimensional stencil on a largeish
        // array?

        if ((N_rank == 2) && (T_expr::numArrayOperands >= 5))
        {
            // Use a heuristic to determine whether a tiled traversal
            // is desirable.  First, estimate how much L1 cache is needed 
            // to achieve a high hit rate using the stack traversal.
            // Try to err on the side of using tiled traversal even when
            // it isn't strictly needed.

            // Assumptions:
            //    Stencil width 3
            //    3 arrays involved in stencil
            //    Uniform data type in arrays (all T_numtype)
            
            int cacheNeeded = 3 * 3 * sizeof(T_numtype) * dest.length(dest.ordering(0));
            if (cacheNeeded > BZ_L1_CACHE_ESTIMATED_SIZE) {
	      _bz_evaluateWithTiled2DTraversal(dest, expr, T_update());
		return;
	    }
        }

#endif

        // If fast traversal isn't available or appropriate, then just
        // do a stack traversal.
//#pragma forceinline recursive
	_bz_evaluator<T_dest::rank_, void /* ignore Policy */>::evaluateWithStackTraversal(dest, expr, T_update());
	return;
    }
}




/* optimized version for Array with array_policy::StaticFortranArrayPolicy */
template<typename P_numtype, int N_rank, int N_base, typename T_expr, typename T_update>
_bz_forceinline void
_bz_evaluate(
    BZ_BLITZ_SCOPE(Array)<P_numtype, N_rank, BZ_BLITZ_SCOPE(array_policy::StaticFortranArrayPolicy<N_base>)>& dest,
    T_expr expr, 
    T_update
)
{
    using T_dest = BZ_BLITZ_SCOPE(Array) < P_numtype, N_rank, BZ_BLITZ_SCOPE(array_policy::StaticFortranArrayPolicy<N_base>) >;
    typedef typename T_dest::T_numtype T_numtype;
    const int N_rank = T_dest::rank_;

    // Check that all arrays have the same shape
#ifdef BZ_DEBUG
    if(!expr.shapeCheck(dest.shape())) {
        if(assertFailMode == false) {
            cerr << "[Blitz++] Shape check failed: Module " << __FILE__
                << " line " << __LINE__ << endl
                << "          Expression: ";
            prettyPrintFormat format(true);   // Use terse formatting
            BZ_STD_SCOPE(string) str;
            expr.prettyPrint(str, format);
            cerr << str << endl;
        }

#if 0
        // Shape dumping is broken by change to using string for prettyPrint
        << "          Shapes: " << shape() << " = ";
        prettyPrintFormat format2;
        format2.setDumpArrayShapesMode();
        expr.prettyPrint(cerr, format2);
        cerr << endl;
#endif
        BZ_PRE_FAIL;
    }
#endif

    BZPRECHECK(expr.shapeCheck(dest.shape()),
        "Shape check failed." << endl << "Expression:");

    BZPRECHECK((T_expr::rank_ == T_dest::rank_) ||
        (T_expr::numArrayOperands == 0),
        "Assigned rank " << T_expr::rank_ << " expression to rank "
        << T_dest::rank_ << " array.");

    /*
    * Check that the arrays are not empty (e.g. length 0 arrays)
    * This fixes a bug found by Peter Bienstman, 6/16/99, where
    * Array<double,2> A(0,0),B(0,0); B=A(tensor::j,tensor::i);
    * went into an infinite loop.
    */

//    const sizeType n = dest.numElements();
//    if(n == 0) {
//#ifdef BZ_DEBUG_TRAVERSE
//        BZ_DEBUG_MESSAGE("Evaluating empty array, nothing to do");
//#endif
//        return;
//    }
//    // \todo this does not alvays compile, so eliminate for now.
//    // if (n == 1) {
//    //   // shortcut here since it's easy
//    //   T_update::update(*dest.dataFirst(), expr(expr.lbound()));
//    //   return;
//    // }
//
//#ifdef BZ_DEBUG_TRAVERSE
//    BZ_DEBUG_MESSAGE("T_expr::numIndexPlaceholders = " << T_expr::numIndexPlaceholders);
//#endif
//
    // Tau profiling code.  Provide Tau with a pretty-printed version of
    // the expression.
    // NEEDS_WORK-- use a static initializer somehow.

#ifdef BZ_TAU_PROFILING
    static BZ_STD_SCOPE(string) exprDescription;
    if(!exprDescription.length())   // faked static initializer
    {
        exprDescription = "A";
        prettyPrintFormat format(true);   // Terse mode on
        format.nextArrayOperandSymbol();
        T_update::prettyPrint(exprDescription);
        expr.prettyPrint(exprDescription, format);
    }
    TAU_PROFILE(" ", exprDescription, TAU_BLITZ);
#endif

    // Determine which evaluation mechanism to use 
    if(T_expr::numIndexPlaceholders > 0) {
        // The expression involves index placeholders, so have to
        // use index traversal rather than stack traversal.

        _bz_evaluator<T_dest::rank_, BZ_BLITZ_SCOPE(array_policy::StaticFortranArrayPolicy<N_base>)>::evaluateWithIndexTraversal(dest, expr, T_update());
        return;
    } else {

        // If this expression looks like an array stencil, then attempt to
        // use a fast traversal order.
        // Fast traversals require <set> from the ISO/ANSI C++ standard
        // library.

#ifdef BZ_HAVE_STD
#ifdef BZ_ARRAY_SPACE_FILLING_TRAVERSAL

        enum {
            isStencil = (N_rank >= 3) && (T_expr::numArrayOperands > 6)
            && (T_expr::numIndexPlaceholders == 0)
        };

        if(_bz_tryFastTraversal<isStencil>::tryFast(dest, expr, T_update()))
            return;

#endif
#endif

#ifdef BZ_ARRAY_2D_STENCIL_TILING
        // Does this look like a 2-dimensional stencil on a largeish
        // array?

        if((N_rank == 2) && (T_expr::numArrayOperands >= 5)) {
            // Use a heuristic to determine whether a tiled traversal
            // is desirable.  First, estimate how much L1 cache is needed 
            // to achieve a high hit rate using the stack traversal.
            // Try to err on the side of using tiled traversal even when
            // it isn't strictly needed.

            // Assumptions:
            //    Stencil width 3
            //    3 arrays involved in stencil
            //    Uniform data type in arrays (all T_numtype)

            int cacheNeeded = 3 * 3 * sizeof(T_numtype) * dest.length(dest.ordering(0));
            if(cacheNeeded > BZ_L1_CACHE_ESTIMATED_SIZE) {
                _bz_evaluateWithTiled2DTraversal(dest, expr, T_update());
                return;
            }
        }

#endif

        // If fast traversal isn't available or appropriate, then just
        // do a stack traversal.
        //#pragma forceinline recursive
        _bz_evaluator<T_dest::rank_, BZ_BLITZ_SCOPE(array_policy::StaticFortranArrayPolicy<N_base >)>::evaluateWithStackTraversal(dest, expr, T_update());
        return;
    }
}





template<typename T_dest, typename T_numtype, typename T_update>
_bz_forceinline void
_bz_evaluate_value(T_dest& dest, const T_numtype& x, T_update) {
    typedef typename T_dest::T_numtype T_numtype;
    const int N_rank = T_dest::rank_;

    /*
    * Check that the arrays are not empty (e.g. length 0 arrays)
    * This fixes a bug found by Peter Bienstman, 6/16/99, where
    * Array<double,2> A(0,0),B(0,0); B=A(tensor::j,tensor::i);
    * went into an infinite loop.
    */

    const sizeType n = dest.numElements();
    if(n == 0) {
#ifdef BZ_DEBUG_TRAVERSE
        BZ_DEBUG_MESSAGE("Evaluating empty array, nothing to do");
#endif
        return;
    }
    // \todo this does not alvays compile, so eliminate for now.
    // if (n == 1) {
    //   // shortcut here since it's easy
    //   T_update::update(*dest.dataFirst(), expr(expr.lbound()));
    //   return;
    // }


    // If fast traversal isn't available or appropriate, then just
    // do a stack traversal.
    //#pragma forceinline recursive
    _bz_evaluator<T_dest::rank_, void /* ignore Policy */>::evaluateValueWithStackTraversal(dest, x, T_update());
}




/* optimized version for Array with array_policy::StaticFortranArrayPolicy */
template<typename P_numtype, int N_rank, int N_base, typename T_numtype, typename T_update>
_bz_forceinline void
_bz_evaluate_value(
    BZ_BLITZ_SCOPE(Array)<P_numtype, N_rank, BZ_BLITZ_SCOPE(array_policy::StaticFortranArrayPolicy<N_base>)>& dest,
    const T_numtype& x,
    T_update
) {
    using T_dest = BZ_BLITZ_SCOPE(Array) < P_numtype, N_rank, BZ_BLITZ_SCOPE(array_policy::StaticFortranArrayPolicy<N_base>) >;
    typedef typename T_dest::T_numtype T_numtype;
    const int N_rank = T_dest::rank_;

    // If fast traversal isn't available or appropriate, then just
    // do a stack traversal.
    //#pragma forceinline recursive
    _bz_evaluator<T_dest::rank_, BZ_BLITZ_SCOPE(array_policy::StaticFortranArrayPolicy<N_base >)>::evaluateValueWithStackTraversal(dest, x, T_update());
}






/** This class performs the vectorized update through the update()
    method. It is a class because it is specialized to do nothing for
    instances where the simd vector width is 1. This avoids tricky
    infinite template recursions on multicomponent containers. */
template<typename T_numtype, typename T_expr, typename T_update, int N>
struct chunked_updater {

  static _bz_forceinline void
  aligned_update(T_numtype* data, T_expr expr, diffType i) {

    const bool unroll = N < BZ_TV_EVALUATE_UNROLL_LENGTH;
    _tv_evaluator<unroll, N>::evaluate_aligned
      (data+i, expr.template fastRead_tv<N>(i), T_update());
  };

  static _bz_forceinline void
  unaligned_update(T_numtype* data, T_expr expr, diffType i) {
    const bool unroll = N < BZ_TV_EVALUATE_UNROLL_LENGTH;
    _tv_evaluator<unroll, N>::evaluate_unaligned
      (data+i, expr.template fastRead_tv<N>(i), T_update());
  };

};

/** specialization ensures we don't try to instantiate chunked_updates
    for types with a vecWidth of 1, as this leads to infinite template
    instantiation recursion. */
template<typename T_numtype, typename T_expr, typename T_update>
struct chunked_updater<T_numtype, T_expr, T_update, 1> {
  static _bz_forceinline void 
  aligned_update(T_numtype* data, T_expr expr, diffType i) {
    BZPRECONDITION(0); };
  static _bz_forceinline void
  unaligned_update(T_numtype* data, T_expr expr, diffType i) {
    BZPRECONDITION(0); };
};


/** A metaprogram that uses the chunked_updater to assign an
    unknown-length expression to a pointer by unrolling in a binary
    fashion. This way, we can get "almost-compile-time" unrolling of a
    length only known at runtime. I+1 is the number of significant
    bits in the longest length to consider. In this way, assigning a
    vector of length 7 is I=2 and will take 3 operations. The
    metaprogram counts down, that way it will start with large updates
    which will be aligned for aligned expressions. */
template<int I> 
class _bz_meta_binaryAssign {
public:
    template<typename T_data, typename T_expr, typename T_update>
    static _bz_forceinline void assign(T_data* data, T_expr expr,
				       diffType ubound, diffType pos, 
				       T_update) {
      if(ubound&(1<<I)) {
	chunked_updater<T_data, T_expr, T_update, 1<<I >::
	  unaligned_update(data, expr, pos); 
	pos += (1<<I);
      }
      _bz_meta_binaryAssign<I-1>::assign(data, expr, ubound, pos, T_update());
      }
        
};

/** Partial specialization for bit 0 uses the scalar update. */
template<> 
class _bz_meta_binaryAssign<0> {
public:
    template<typename T_data, typename T_expr, typename T_update>
    static _bz_forceinline void assign(T_data* data, T_expr expr,
				       diffType ubound, diffType pos, 
				       T_update) {
      if(ubound&1) {
	T_update::update(data[pos], expr.fastRead(pos));
	++pos;
      }
      // this ends the metaprogram.
    }
};

/** Unit-stride evaluator, which takes pre-computed destination and
    bounds and just does the unit-stride evaluation for a specified
    length. This is essentially a 1D operation used by both the rank-1
    and rank-N traversals, so it's common to both. This can use
    vectorized update, so if both dest and expr are unit stride, we
    redirect here. This function then deals with unaligned or
    misaligned situations. There is no explicit unrolling option here,
    since it's already vectorized using the chunk_updater. \todo Would
    it be useful to retain the unrolled loop for scalar
    architectures?  */
template<typename T_dest, typename T_expr, typename T_update>
_bz_forceinline void
_bz_evaluateWithUnitStride(T_dest& dest, typename T_dest::T_iterator& iter,
			   T_expr expr, diffType ubound, T_update)
{
  typedef typename T_dest::T_numtype T_numtype;
  T_numtype* _bz_restrict data = const_cast<T_numtype*>(iter.data());
  diffType i=0;
#ifdef BZ_DO_NOT_MESS_WITH_THE_OPTIMIZER
  for(; i < ubound; ++i)
      //#pragma forceinline recursive
      T_update::update(data[i], expr.fastRead(i));

#else
#ifdef BZ_DEBUG_TRAVERSE
  BZ_DEBUG_MESSAGE("\tunit stride expression with length: "<< ubound << ".");
#endif

  // If the minWidth is set to 0, there are elements in the expression
  // which can NOT use the vectorized expression (i.e, stencils). In
  // that case, we fall through to the scalar loop
  const bool unvectorizable = (T_expr::minWidth==0);

  if(!unvectorizable && (ubound < 1<<BZ_MAX_BITS_FOR_BINARY_UNROLL)) {
    // for short expressions, it's more important to lose
    // overhead. Single-element ones have already been dealt with, but
    // for lengths that are have fewer significant bits than
    // max_bits_for_unroll we do a binary-style unroll here. (We don't
    // worry about simd widths either, because we essentially just
    // present the compiler with a vectorizable view. It will do
    // sensible things even if the expressions are not vectorizable.)
#ifdef BZ_DEBUG_TRAVERSE
    BZ_DEBUG_MESSAGE("\tshort expression, using binary meta-unroll assignment.");
#endif

    _bz_meta_binaryAssign<BZ_MAX_BITS_FOR_BINARY_UNROLL-1>::
      assign(data, expr, ubound, 0, T_update());
    return;
  }

  // calculate uneven elements at the beginning of dest
  const diffType uneven_start=simdTypes<T_numtype>::offsetToAlignment(data);

  // we can only guarantee alignment if all operands have the same
  // width and are not mutually misaligned
  const bool can_align = 
    (T_expr::minWidth == T_expr::maxWidth) &&
    (T_expr::minWidth == int(simdTypes<T_numtype>::vecWidth)) &&
    expr.isVectorAligned(uneven_start);

  // When we come out here, we KNOW that expressions shorter than
  // 1<<BZ_MAX_BITS_FOR_BINARY_UNROLL have been taken care of. At that
  // point, it is efficient to effectively unroll the loop using a
  // vector width larger than the simd width.
  const int loop_width= BZ_VECTORIZED_LOOP_WIDTH;

#ifdef BZ_DEBUG_TRAVERSE
  if(T_expr::minWidth!=T_expr::maxWidth) {
    BZ_DEBUG_MESSAGE("\texpression has mixed width: " << T_expr::minWidth << "-" <<T_expr::maxWidth);
  } else {
    BZ_DEBUG_MESSAGE("\texpression SIMD width: " << T_expr::minWidth);
  }
  BZ_DEBUG_MESSAGE("\tdestination SIMD width: " << simdTypes<T_numtype>::vecWidth);
  if(loop_width>1) {
  if(!expr.isVectorAligned(uneven_start)) {
    BZ_DEBUG_MESSAGE("\toperands have different alignments");
  }
  if(!can_align) {
    BZ_DEBUG_MESSAGE("\tcannot guarantee alignment - using unaligned vectorization")
      } else {
    BZ_DEBUG_MESSAGE("\texpression can be aligned");
  }
  if(loop_width<=ubound) {
    BZ_DEBUG_MESSAGE("\tusing vectorization width " << loop_width);
  } else {
    BZ_DEBUG_MESSAGE("\texpression not long enough to be vectorized");
  }
  } else {
    BZ_DEBUG_MESSAGE("\texpression cannot be vectorized");
  }
#endif


  if(!unvectorizable && (loop_width>1)) {
    // If the expression can be aligned, we do so.
    if(can_align) {
#ifdef BZ_DEBUG_TRAVERSE
      if(i<uneven_start) {
	BZ_DEBUG_MESSAGE("\tscalar loop for " << uneven_start << " unaligned starting elements");
      }
#endif
#ifdef BZ_USE_ALIGNMENT_PRAGMAS
#pragma ivdep
#endif
      for (; i < uneven_start; ++i)
//#pragma forceinline recursive
	T_update::update(data[i], expr.fastRead(i));
      
      // and then the vectorized part
#ifdef BZ_DEBUG_TRAVERSE
      if(i<=ubound-loop_width) {
	BZ_DEBUG_MESSAGE("\taligned vectorized loop with width " << loop_width << " starting at " << i);
      }
#endif
      for (; i <= ubound-loop_width; i+=loop_width)
//#pragma forceinline recursive
	chunked_updater<T_numtype, T_expr, T_update, loop_width>::
	  aligned_update(data, expr, i);
    }
    else {
      // if we can not line up the expressions, alignment doesn't
      // matter and we just start using unaligned vectorized
      // instructions from element 0
#ifdef BZ_DEBUG_TRAVERSE
      if(i<=ubound-loop_width) {
	BZ_DEBUG_MESSAGE("\tunaligned vectorized loop with width " << loop_width << " starting at " << i);
      }
#endif
      for (; i <= ubound-loop_width; i+=loop_width)
//#pragma forceinline recursive
	chunked_updater<T_numtype, T_expr, T_update, loop_width>::
	  unaligned_update(data, expr, i);
    }
  }

  // now complete the loop with the tailing scalar elements not done
  // in the chunked loop.
#ifdef BZ_DEBUG_TRAVERSE
  if(i<ubound) {
    BZ_DEBUG_MESSAGE("\tscalar loop for " << ubound-i << " trailing elements starting at " << i);
  }
#endif
#ifdef BZ_USE_ALIGNMENT_PRAGMAS
#pragma ivdep
#endif
  for (; i < ubound; ++i)
//#pragma forceinline recursive
    T_update::update(data[i], expr.fastRead(i));

#ifdef BZ_DEBUG_TRAVERSE
  BZ_DEBUG_MESSAGE("\tunit stride evaluation done")
#endif
#endif // BZ_DO_NOT_MESS_WITH_THE_OPTIMIZER
}


/** Common-stride evaluator. Used for common but non-unit
    strides. Note that the stride can be negative, so we need to use a
    signed type. */
template<typename T_dest, typename T_expr, typename T_update>
_bz_forceinline void
_bz_evaluateWithCommonStride(T_dest& dest, typename T_dest::T_iterator& iter,
			     T_expr expr, diffType ubound, 
			     diffType commonStride, 
			     T_update)
{
#ifdef BZ_DEBUG_TRAVERSE 
     BZ_DEBUG_MESSAGE("\tcommon stride = " << commonStride);
#endif

  typedef typename T_dest::T_numtype T_numtype;
  T_numtype* _bz_restrict data = const_cast<T_numtype*>(iter.data());

#ifndef BZ_ARRAY_STACK_TRAVERSAL_UNROLL
# ifdef BZ_USE_ALIGNMENT_PRAGMAS
# pragma ivdep
# endif
  for (diffType i=0; i != ubound; i += commonStride)
    T_update::update(data[i], expr.fastRead(i));
#else
  diffType n1 = (dest.length(firstRank) & 3) * commonStride;
	 
  diffType i = 0;
  for (; i != n1; i += commonStride)
    T_update::update(data[i], expr.fastRead(i));
	 
  diffType strideInc = 4 * commonStride;
  for (; i != ubound; i += strideInc)
    {
      T_update::update(data[i], expr.fastRead(i));
      diffType i2 = i + commonStride;
      T_update::update(data[i2], expr.fastRead(i2));
      diffType i3 = i + 2 * commonStride;
      T_update::update(data[i3], expr.fastRead(i3));
      diffType i4 = i + 3 * commonStride;
      T_update::update(data[i4], expr.fastRead(i4));
    }
#endif  // BZ_ARRAY_STACK_TRAVERSAL_UNROLL
  return;
}




/** Unit-stride evaluator, which takes pre-computed destination and
bounds and just does the unit-stride evaluation for a specified
length. This is essentially a 1D operation used by both the rank-1
and rank-N traversals, so it's common to both. This can use
vectorized update, so if both dest and expr are unit stride, we
redirect here. This function then deals with unaligned or
misaligned situations. There is no explicit unrolling option here,
since it's already vectorized using the chunk_updater. \todo Would
it be useful to retain the unrolled loop for scalar
architectures?  */
template<typename T_dest, typename T_numtype, typename T_update>
_bz_forceinline void
_bz_evaluateValueWithUnitStride(T_dest& dest, typename T_dest::T_iterator& iter,
    const T_numtype& x, diffType ubound, T_update) {
    typedef typename T_dest::T_numtype T_numtype;
    T_numtype* _bz_restrict data = const_cast<T_numtype*>(iter.data());
    

    for(diffType i = 0; i < ubound; ++i)
        //#pragma forceinline recursive
        T_update::update(data[i], x);

}


/** Common-stride evaluator. Used for common but non-unit
strides. Note that the stride can be negative, so we need to use a
signed type. */
template<typename T_dest, typename T_numtype, typename T_update>
_bz_forceinline void
_bz_evaluateValueWithCommonStride(T_dest& dest, typename T_dest::T_iterator& iter,
    const T_numtype& x, diffType ubound,
    diffType commonStride,
    T_update) {
#ifdef BZ_DEBUG_TRAVERSE 
    BZ_DEBUG_MESSAGE("\tcommon stride = " << commonStride);
#endif

    typedef typename T_dest::T_numtype T_numtype;
    T_numtype* _bz_restrict data = const_cast<T_numtype*>(iter.data());

# ifdef BZ_USE_ALIGNMENT_PRAGMAS
# pragma ivdep
# endif
    for(diffType i = 0; i != ubound; i += commonStride)
        T_update::update(data[i], x);
}




/* 1-d stack traversal evaluation. Forwards to evaluateWithUnitStride
   or evaluateWithCommonStride, if applicable, otherwise does the slow
   different-stride update. */
template<int N_base>
template<typename T_dest, typename T_expr, typename T_update>
_bz_forceinline void
_bz_evaluator<1, BZ_BLITZ_SCOPE(array_policy::StaticFortranArrayPolicy<N_base>)>::
evaluateWithStackTraversal(T_dest& dest, T_expr expr, T_update)
{
#ifdef BZ_DEBUG_TRAVERSE
    BZ_DEBUG_MESSAGE("_bz_evaluator<1>: Using stack traversal");
#endif

    typename T_dest::T_iterator iter(dest);

#ifndef BZ_DO_NOT_MESS_WITH_THE_OPTIMIZER
    // if we only have one element, strides don't matter. In that case,
    // we just evaluate that right now so we don't have to deal with it.
    if(dest.length(firstRank) == 1) {
#ifdef BZ_DEBUG_TRAVERSE
        BZ_DEBUG_MESSAGE("\tshortcutting evaluation of single-element expression");
#endif
        T_update::update(*const_cast<typename T_dest::T_numtype*>(iter.data()), *expr);
        return;
    }
#endif // BZ_DO_NOT_MESS_WITH_THE_OPTIMIZER

    iter.loadStride(firstRank);
    expr.loadStride(firstRank);

#ifndef BZ_DO_NOT_MESS_WITH_THE_OPTIMIZER
    const bool useUnitStride = iter.isUnitStride()
        && expr.isUnitStride();

    if(useUnitStride) {
        const diffType ubound = dest.length(firstRank);
        _bz_evaluateWithUnitStride(dest, iter, expr, ubound, T_update());
        return;
    }

#ifdef BZ_ARRAY_EXPR_USE_COMMON_STRIDE
    diffType commonStride = expr.suggestStride(firstRank);
    if(iter.suggestStride(firstRank) > commonStride)
        commonStride = iter.suggestStride(firstRank);
    bool useCommonStride = iter.isStride(firstRank, commonStride)
        && expr.isStride(firstRank, commonStride);
#else
    diffType commonStride = 1;
    bool useCommonStride = false;
#endif

    if(useCommonStride) {
        const diffType ubound = dest.length(firstRank) * commonStride;
        _bz_evaluateWithCommonStride(dest, iter, expr, ubound, commonStride, T_update());
        return;
    }

#ifdef BZ_DEBUG_TRAVERSE
    BZ_DEBUG_MESSAGE("\tnot common stride");
#endif

#endif // BZ_DO_NOT_MESS_WITH_THE_OPTIMIZER

    // not common stride
    typedef typename T_dest::T_numtype T_numtype;
    const T_numtype * last = iter.data() + dest.length(firstRank)
        * dest.stride(firstRank);

    while(iter.data() != last) {
        T_update::update(*const_cast<T_numtype*>(iter.data()), *expr);
        iter.advance();
        expr.advance();
    }
}

template<typename DestPolicy> 
template<typename T_dest, typename T_expr, typename T_update>
_bz_forceinline void
_bz_evaluator<1, DestPolicy>::
evaluateWithStackTraversal(T_dest& dest, T_expr expr, T_update)
{
#ifdef BZ_DEBUG_TRAVERSE
  BZ_DEBUG_MESSAGE("_bz_evaluator<1>: Using stack traversal");
#endif

  typename T_dest::T_iterator iter(dest);

#ifndef BZ_DO_NOT_MESS_WITH_THE_OPTIMIZER
  // if we only have one element, strides don't matter. In that case,
  // we just evaluate that right now so we don't have to deal with it.
  if(dest.length(firstRank)==1) {
#ifdef BZ_DEBUG_TRAVERSE
  BZ_DEBUG_MESSAGE("\tshortcutting evaluation of single-element expression");
#endif
    T_update::update(*const_cast<typename T_dest::T_numtype*>(iter.data()), *expr);
    return;
  }
#endif // BZ_DO_NOT_MESS_WITH_THE_OPTIMIZER

  iter.loadStride(firstRank);
  expr.loadStride(firstRank);

#ifndef BZ_DO_NOT_MESS_WITH_THE_OPTIMIZER
  const bool useUnitStride = iter.isUnitStride()
    && expr.isUnitStride();

  if(useUnitStride) {
    const diffType ubound = dest.length(firstRank);
    _bz_evaluateWithUnitStride(dest, iter, expr, ubound, T_update());
    return;
  }

#ifdef BZ_ARRAY_EXPR_USE_COMMON_STRIDE
  diffType commonStride = expr.suggestStride(firstRank);
  if (iter.suggestStride(firstRank) > commonStride)
    commonStride = iter.suggestStride(firstRank);
  bool useCommonStride = iter.isStride(firstRank,commonStride)
    && expr.isStride(firstRank,commonStride);
#else
  diffType commonStride = 1;
  bool useCommonStride = false;
#endif

  if (useCommonStride) {
    const diffType ubound = dest.length(firstRank) * commonStride;
    _bz_evaluateWithCommonStride(dest, iter, expr, ubound, commonStride, T_update());
    return;
  }

#ifdef BZ_DEBUG_TRAVERSE
  BZ_DEBUG_MESSAGE("\tnot common stride");
#endif

#endif // BZ_DO_NOT_MESS_WITH_THE_OPTIMIZER

  // not common stride
  typedef typename T_dest::T_numtype T_numtype;
  const T_numtype * last = iter.data() + dest.length(firstRank) 
    * dest.stride(firstRank);
     
  while (iter.data() != last)
    {
      T_update::update(*const_cast<T_numtype*>(iter.data()), *expr);
      iter.advance();
      expr.advance();
    }
}


/**
   Perform a stack traversal of a rank >1 expression. A stack
   traversal replaces the usual nested loops:
      
   for (int i=A.lbound(firstDim); i <= A.ubound(firstDim); ++i)
     for (int j=A.lbound(secondDim); j <= A.ubound(secondDim); ++j)
       for (int k=A.lbound(thirdDim); k <= A.ubound(thirdDim); ++k)
         A(i,j,k) = 0;
      
   with a stack data structure.  The stack allows this single routine
   to replace any number of nested loops.
      
   For each dimension (loop), these quantities are needed:
   - a pointer to the first element encountered in the loop
   - the stride associated with the dimension/loop
   - a pointer to the last element encountered in the loop
   
   The basic idea is that entering each loop is a "push" onto the
   stack, and exiting each loop is a "pop".  In practice, this
   routine treats accesses the stack in a random-access way,
   which confuses the picture a bit.  But conceptually, that's
   what is going on.

   ordering(0) gives the dimension associated with the smallest
   stride (usually; the exceptions have to do with subarrays and
   are uninteresting).  We call this dimension maxRank; it will
   become the innermost "loop".
      
   Ordering the loops from ordering(N_rank-1) down to
   ordering(0) ensures that the largest stride is associated
   with the outermost loop, and the smallest stride with the
   innermost.  This is critical for good performance on
   cached machines.
*/

template<int N, typename DestPolicy>
template<typename T_dest, typename T_expr, typename T_update>
_bz_forceinline void
_bz_evaluator<N, DestPolicy>::
evaluateWithStackTraversal(T_dest& dest, T_expr expr, T_update)
 {
#ifdef BZ_DEBUG_TRAVERSE
   BZ_DEBUG_MESSAGE("_bz_evaluator<" << N << ">: Using stack traversal");
#endif

   typedef typename T_dest::T_numtype T_numtype;
   const int N_rank = T_dest::rank();

     const int maxRank = dest.ordering(0);
     // const int secondLastRank = ordering(1);

     // Create an iterator for the array receiving the result
     typename T_dest::T_iterator iter(dest);

     // Set the initial stack configuration by pushing the pointer
     // to the first element of the array onto the stack N times.

     int i;
     for (i=1; i < N_rank; ++i)
     {
	 iter.push(i);
	 expr.push(i);
     }

     // Load the strides associated with the innermost loop.
     iter.loadStride(maxRank);
     expr.loadStride(maxRank);

     /* 
      * Is the stride in the innermost loop equal to 1?  If so,
      * we might take advantage of this and generate more
      * efficient code.
      */
     const bool useUnitStride = iter.isUnitStride()
                          && expr.isUnitStride();

    /*
     * Do all array operands share a common stride in the innermost
     * loop?  If so, we can generate more efficient code (but only
     * if this optimization has been enabled).
     */
#ifdef BZ_ARRAY_EXPR_USE_COMMON_STRIDE
    diffType commonStride = expr.suggestStride(maxRank);
    if (iter.suggestStride(maxRank) > commonStride)
        commonStride = iter.suggestStride(maxRank);
    bool useCommonStride = iter.isStride(maxRank,commonStride)
        && expr.isStride(maxRank,commonStride);

#ifdef BZ_DEBUG_TRAVERSE
    BZ_DEBUG_MESSAGE("BZ_ARRAY_EXPR_USE_COMMON_STRIDE" << endl
        << "commonStride = " << commonStride << " useCommonStride = "
        << useCommonStride);
#endif

#else
    const diffType commonStride = 1;
    const bool useCommonStride = false;
#endif

    /*
     * The "last" array contains a pointer to the last element
     * encountered in each "loop".
     */
    const T_numtype* last[T_dest::rank_];

    // Set up the initial state of the "last" array
    for (i=1; i < N_rank; ++i)
        last[i] = iter.data() + dest.length(dest.ordering(i)) * dest.stride(dest.ordering(i));

    diffType lastLength = dest.length(maxRank);
    int firstNoncollapsedLoop = 1;

#ifdef BZ_COLLAPSE_LOOPS

    /*
     * This bit of code handles collapsing loops.  When possible,
     * the N nested loops are converted into a single loop (basically,
     * the N-dimensional array is treated as a long vector).
     * This is important for cases where the length of the innermost
     * loop is very small, for example a 100x100x3 array.
     * If this code can't collapse all the loops into a single loop,
     * it will collapse as many loops as possible starting from the
     * innermost and working out.
     */

    // Collapse loops when possible
    for (i=1; i < N_rank; ++i)
    {
        // Figure out which pair of loops we are considering combining.
        int outerLoopRank = iter.ordering(i);
        int innerLoopRank = iter.ordering(i-1);

        /*
         * The canCollapse() routines look at the strides and extents
         * of the loops, and determine if they can be combined into
         * one loop.
         */

        if (iter.canCollapse(outerLoopRank,innerLoopRank) 
          && expr.canCollapse(outerLoopRank,innerLoopRank))
        {
#ifdef BZ_DEBUG_TRAVERSE
            cout << "Collapsing " << outerLoopRank << " and " 
                 << innerLoopRank << endl;
#endif
            lastLength *= dest.length(outerLoopRank);
            firstNoncollapsedLoop = i+1;
        }
        else  
            break;
    }

#endif // BZ_COLLAPSE_LOOPS

    /*
     * Now we actually perform the loops.  This while loop contains
     * two parts: first, the innermost loop is performed.  Then we
     * exit the loop, and pop our way down the stack until we find
     * a loop that isn't completed.  We then restart the inner loops
     * and push them onto the stack.
     */

    while (true) {

        /*
         * This bit of code handles the innermost loop.  It just uses
         * the separate evaluation functions depeding on the
         * stride. */

      diffType ubound = lastLength * commonStride;
      
      if (useUnitStride || useCommonStride) {
	if(useUnitStride)
	  _bz_evaluateWithUnitStride(dest, iter, expr, ubound, T_update());
	else 
	  _bz_evaluateWithCommonStride(dest, iter, expr, ubound, commonStride,
				       T_update());

	/*
	 * Tidy up for the fact that we haven't actually been
	 * incrementing the iterators in the innermost loop, by
	 * faking it afterward.
	 */
    iter.advance(static_cast<_bz__int>(lastLength * commonStride));
    expr.advance(static_cast<_bz__int>(lastLength * commonStride));
      }
      else {
            /*
             * We don't have a unit stride or common stride in the innermost
             * loop.  This is going to hurt performance.  Luckily 95% of
             * the time, we hit the cases above.
             */
            T_numtype * _bz_restrict end = const_cast<T_numtype*>(iter.data())
                + lastLength * dest.stride(maxRank);

            while (iter.data() != end)
            {
	      T_update::update(*const_cast<T_numtype*>(iter.data()), *expr);
                iter.advance();
                expr.advance();
            }
        }


        /*
         * We just finished the innermost loop.  Now we pop our way down
         * the stack, until we hit a loop that hasn't completed yet.
         */ 
        int j = firstNoncollapsedLoop;
        for (; j < N_rank; ++j)
        {
            // Get the next loop
            int r = dest.ordering(j);

            // Pop-- this restores the data pointers to the first element
            // encountered in the loop.
            iter.pop(j);
            expr.pop(j);

            // Load the stride associated with this loop, and increment
            // once.
            iter.loadStride(r);
            expr.loadStride(r);
            iter.advance();
            expr.advance();

            // If we aren't at the end of this loop, then stop popping.
            if (iter.data() != last[j])
                break;
        }

        // Are we completely done?
        if (j == N_rank)
            break;

        // No, so push all the inner loops back onto the stack.
        for (; j >= firstNoncollapsedLoop; --j)
        {
            int r2 = dest.ordering(j-1);
            iter.push(j);
            expr.push(j);
            last[j-1] = iter.data() + dest.length(r2) * dest.stride(r2);
        }

        // Load the stride for the innermost loop again.
        iter.loadStride(maxRank);
        expr.loadStride(maxRank);
    }
}




/* 1-d stack traversal evaluation. Forwards to evaluateWithUnitStride
or evaluateWithCommonStride, if applicable, otherwise does the slow
different-stride update. */
template<int N_base>
template<typename T_dest, typename T_numtype, typename T_update>
_bz_forceinline void
_bz_evaluator<1, BZ_BLITZ_SCOPE(array_policy::StaticFortranArrayPolicy<N_base>)>::
evaluateValueWithStackTraversal(T_dest& dest, const T_numtype& x, T_update) {
#ifdef BZ_DEBUG_TRAVERSE
    BZ_DEBUG_MESSAGE("_bz_evaluator<1>: Using stack traversal");
#endif
    typedef typename T_dest::T_numtype T_numtype;
    typename T_dest::T_iterator iter(dest);
    T_numtype* _bz_restrict data = const_cast<T_numtype*>(iter.data());


    for(diffType i = 0; i < dest.length(firstRank); ++i)
        //#pragma forceinline recursive
        T_update::update(data[i], x);
    return;

    //typename T_dest::T_iterator iter(dest);

    //iter.loadStride(firstRank);

    //// not common stride
    //typedef typename T_dest::T_numtype T_numtype;
    //const T_numtype * last = iter.data() + dest.length(firstRank)
    //    * dest.stride(firstRank);

    //while(iter.data() != last) {
    //    T_update::update(*const_cast<T_numtype*>(iter.data()), x);
    //    iter.advance();
    //}
}

template<typename DestPolicy>
template<typename T_dest, typename T_numtype, typename T_update>
_bz_forceinline void
_bz_evaluator<1, DestPolicy>::
evaluateValueWithStackTraversal(T_dest& dest, const T_numtype& x, T_update) {
#ifdef BZ_DEBUG_TRAVERSE
    BZ_DEBUG_MESSAGE("_bz_evaluator<1>: Using stack traversal");
#endif

    typename T_dest::T_iterator iter(dest);

    iter.loadStride(firstRank);

    // not common stride
    typedef typename T_dest::T_numtype T_numtype;
    const T_numtype * last = iter.data() + dest.length(firstRank)
        * dest.stride(firstRank);

    while(iter.data() != last) {
        T_update::update(*const_cast<T_numtype*>(iter.data()), x);
        iter.advance();
    }
}


/**
Perform a stack traversal of a rank >1 expression. A stack
traversal replaces the usual nested loops:

for (int i=A.lbound(firstDim); i <= A.ubound(firstDim); ++i)
for (int j=A.lbound(secondDim); j <= A.ubound(secondDim); ++j)
for (int k=A.lbound(thirdDim); k <= A.ubound(thirdDim); ++k)
A(i,j,k) = 0;

with a stack data structure.  The stack allows this single routine
to replace any number of nested loops.

For each dimension (loop), these quantities are needed:
- a pointer to the first element encountered in the loop
- the stride associated with the dimension/loop
- a pointer to the last element encountered in the loop

The basic idea is that entering each loop is a "push" onto the
stack, and exiting each loop is a "pop".  In practice, this
routine treats accesses the stack in a random-access way,
which confuses the picture a bit.  But conceptually, that's
what is going on.

ordering(0) gives the dimension associated with the smallest
stride (usually; the exceptions have to do with subarrays and
are uninteresting).  We call this dimension maxRank; it will
become the innermost "loop".

Ordering the loops from ordering(N_rank-1) down to
ordering(0) ensures that the largest stride is associated
with the outermost loop, and the smallest stride with the
innermost.  This is critical for good performance on
cached machines.
*/

template<int N, typename DestPolicy>
template<typename T_dest, typename T_numtype, typename T_update>
_bz_forceinline void
_bz_evaluator<N, DestPolicy>::
evaluateValueWithStackTraversal(T_dest& dest, const T_numtype& x, T_update) {
#ifdef BZ_DEBUG_TRAVERSE
    BZ_DEBUG_MESSAGE("_bz_evaluator<" << N << ">: Using stack traversal");
#endif

    typedef typename T_dest::T_numtype T_numtype;
    const int N_rank = T_dest::rank();

    const int maxRank = dest.ordering(0);
    // const int secondLastRank = ordering(1);

    // Create an iterator for the array receiving the result
    typename T_dest::T_iterator iter(dest);

    // Set the initial stack configuration by pushing the pointer
    // to the first element of the array onto the stack N times.

    int i;
    for(i = 1; i < N_rank; ++i) {
        iter.push(i);
    }

    // Load the strides associated with the innermost loop.
    iter.loadStride(maxRank);

    /*
    * Is the stride in the innermost loop equal to 1?  If so,
    * we might take advantage of this and generate more
    * efficient code.
    */
    const bool useUnitStride = iter.isUnitStride()
        ;

    /*
    * Do all array operands share a common stride in the innermost
    * loop?  If so, we can generate more efficient code (but only
    * if this optimization has been enabled).
    */
    diffType commonStride = iter.suggestStride(maxRank);
    static constexpr bool useCommonStride = true;

    /*
    * The "last" array contains a pointer to the last element
    * encountered in each "loop".
    */
    const T_numtype* last[T_dest::rank_];

    // Set up the initial state of the "last" array
    for(i = 1; i < N_rank; ++i)
        last[i] = iter.data() + dest.length(dest.ordering(i)) * dest.stride(dest.ordering(i));

    diffType lastLength = dest.length(maxRank);
    int firstNoncollapsedLoop = 1;

#ifdef BZ_COLLAPSE_LOOPS

    /*
    * This bit of code handles collapsing loops.  When possible,
    * the N nested loops are converted into a single loop (basically,
    * the N-dimensional array is treated as a long vector).
    * This is important for cases where the length of the innermost
    * loop is very small, for example a 100x100x3 array.
    * If this code can't collapse all the loops into a single loop,
    * it will collapse as many loops as possible starting from the
    * innermost and working out.
    */

    // Collapse loops when possible
    for(i = 1; i < N_rank; ++i) {
        // Figure out which pair of loops we are considering combining.
        int outerLoopRank = iter.ordering(i);
        int innerLoopRank = iter.ordering(i - 1);

        /*
        * The canCollapse() routines look at the strides and extents
        * of the loops, and determine if they can be combined into
        * one loop.
        */

        if(iter.canCollapse(outerLoopRank, innerLoopRank)) {
#ifdef BZ_DEBUG_TRAVERSE
            cout << "Collapsing " << outerLoopRank << " and "
                << innerLoopRank << endl;
#endif
            lastLength *= dest.length(outerLoopRank);
            firstNoncollapsedLoop = i + 1;
        } else
            break;
    }

#endif // BZ_COLLAPSE_LOOPS

    /*
    * Now we actually perform the loops.  This while loop contains
    * two parts: first, the innermost loop is performed.  Then we
    * exit the loop, and pop our way down the stack until we find
    * a loop that isn't completed.  We then restart the inner loops
    * and push them onto the stack.
    */

    while(true) {

        /*
        * This bit of code handles the innermost loop.  It just uses
        * the separate evaluation functions depeding on the
        * stride. */

        diffType ubound = lastLength * commonStride;

        if(useUnitStride)
            _bz_evaluateValueWithUnitStride(dest, iter, x, ubound, T_update());
        else
            _bz_evaluateValueWithCommonStride(dest, iter, x, ubound, commonStride,
                T_update());

        /*
        * Tidy up for the fact that we haven't actually been
        * incrementing the iterators in the innermost loop, by
        * faking it afterward.
        */
        iter.advance(static_cast<_bz__int>(lastLength * commonStride));

        /*
        * We just finished the innermost loop.  Now we pop our way down
        * the stack, until we hit a loop that hasn't completed yet.
        */
        int j = firstNoncollapsedLoop;
        for(; j < N_rank; ++j) {
            // Get the next loop
            int r = dest.ordering(j);

            // Pop-- this restores the data pointers to the first element
            // encountered in the loop.
            iter.pop(j);

            // Load the stride associated with this loop, and increment
            // once.
            iter.loadStride(r);
            iter.advance();

            // If we aren't at the end of this loop, then stop popping.
            if(iter.data() != last[j])
                break;
        }

        // Are we completely done?
        if(j == N_rank)
            break;

        // No, so push all the inner loops back onto the stack.
        for(; j >= firstNoncollapsedLoop; --j) {
            int r2 = dest.ordering(j - 1);
            iter.push(j);
            last[j - 1] = iter.data() + dest.length(r2) * dest.stride(r2);
        }

        // Load the stride for the innermost loop again.
        iter.loadStride(maxRank);
    }
}





template<int N_base>
template<typename T_dest, typename T_expr, typename T_update>
_bz_forceinline void
_bz_evaluator<1, BZ_BLITZ_SCOPE(array_policy::StaticFortranArrayPolicy<N_base>)>::
evaluateWithIndexTraversal(T_dest& dest, T_expr expr, T_update) {
    typedef typename T_dest::T_numtype T_numtype;

    TinyVector<int, T_dest::rank_> index;

    if(dest.stride(firstRank) == 1) {
        T_numtype * _bz_restrict iter = dest.data();
        int last = dest.ubound(firstRank);

        for(index[0] = dest.lbound(firstRank); index[0] <= last;
            ++index[0]) {
            T_update::update(*iter++, expr(index));
        }
    } else {
        typename T_dest::T_iterator iter(dest);
        iter.loadStride(0);
        int last = iter.ubound(firstRank);

        for(index[0] = iter.lbound(firstRank); index[0] <= last;
            ++index[0]) {
            T_update::update(*const_cast<T_numtype*>(iter.data()),
                expr(index));
            iter.advance();
        }
    }
}

template<typename DestPolicy>
template<typename T_dest, typename T_expr, typename T_update>
_bz_forceinline void
_bz_evaluator<1, DestPolicy>::
evaluateWithIndexTraversal(T_dest& dest, T_expr expr, T_update)
{
  typedef typename T_dest::T_numtype T_numtype;

  TinyVector<int,T_dest::rank_> index;

  if (dest.stride(firstRank) == 1) {
    T_numtype * _bz_restrict iter = dest.data();
    int last = dest.ubound(firstRank);
    
    for (index[0] = dest.lbound(firstRank); index[0] <= last;
	 ++index[0]) {
      T_update::update(*iter++, expr(index));
    }
  }
  else {
    typename T_dest::T_iterator iter(dest);
    iter.loadStride(0);
    int last = iter.ubound(firstRank);
    
    for (index[0] = iter.lbound(firstRank); index[0] <= last;
	 ++index[0]) {
      T_update::update(*const_cast<T_numtype*>(iter.data()), 
		       expr(index));
      iter.advance();
    }
  }
}

template<int N, typename DestPolicy>
template<typename T_dest, typename T_expr, typename T_update>
_bz_forceinline void
_bz_evaluator<N, DestPolicy>::
evaluateWithIndexTraversal(T_dest& dest, T_expr expr, T_update)
{
  typedef typename T_dest::T_numtype T_numtype;
  const int N_rank = T_dest::rank();

    // Do a stack-type traversal for the destination array and use
    // index traversal for the source expression
   
    const int maxRank = dest.ordering(0);

#ifdef BZ_DEBUG_TRAVERSE
    const int secondLastRank = dest.ordering(1);
    cout << "Index traversal: N_rank = " << N_rank << endl;
    cout << "maxRank = " << maxRank << " secondLastRank = " << secondLastRank
         << endl;
    cout.flush();
#endif

    typename T_dest::T_iterator iter(dest);
    for (int i=1; i < N_rank; ++i)
        iter.push(iter.ordering(i));

    iter.loadStride(maxRank);

    TinyVector<int,T_dest::rank_> index, last;

    index = dest.base();

    for (int i=0; i < N_rank; ++i)
      last(i) = dest.base(i) + dest.length(i);

    // int lastLength = length(maxRank);

    while (true) {

        for (index[maxRank] = dest.base(maxRank); 
             index[maxRank] < last[maxRank]; 
             ++index[maxRank])
        {
#ifdef BZ_DEBUG_TRAVERSE
#if 0
    cout << "(" << index[0] << "," << index[1] << ") " << endl;
    cout.flush();
#endif
#endif

            T_update::update(*const_cast<T_numtype*>(iter.data()), expr(index));
            iter.advance();
        }

        int j = 1;
        for (; j < N_rank; ++j)
        {
            iter.pop(dest.ordering(j));
            iter.loadStride(dest.ordering(j));
            iter.advance();

            index[dest.ordering(j-1)] = dest.base(dest.ordering(j-1));
            ++index[dest.ordering(j)];
            if (index[dest.ordering(j)] != last[dest.ordering(j)])
                break;
        }

        if (j == N_rank)
            break;

        for (; j > 0; --j)
        {
            iter.push(dest.ordering(j));
        }
        iter.loadStride(maxRank);
    }
}

// Fast traversals require <set> from the ISO/ANSI C++ standard library

#ifdef BZ_HAVE_STD
#ifdef BZ_ARRAY_SPACE_FILLING_TRAVERSAL

template<typename T_dest, typename T_expr, typename T_update>
_bz_forceinline void
_bz_evaluateWithFastTraversal(T_dest& dest,     
			      const TraversalOrder<T_dest::rank() - 1>& order, 
			      T_expr expr, T_update)
{
  typedef typename T_dest::T_numtype T_numtype;
  const int N_rank = T_dest::rank();

    const int maxRank = dest.ordering(0);

#ifdef BZ_DEBUG_TRAVERSE
    const int secondLastRank = dest.ordering(1);
    cerr << "maxRank = " << maxRank << " secondLastRank = " << secondLastRank
         << endl;
#endif

    T_dest::T_iterator iter(dest);
    iter.push(0);
    expr.push(0);

    bool useUnitStride = iter.isUnitStride(maxRank) 
                          && expr.isUnitStride(maxRank);

#ifdef BZ_ARRAY_EXPR_USE_COMMON_STRIDE
    diffType commonStride = expr.suggestStride(maxRank);
    if (iter.suggestStride(maxRank) > commonStride)
        commonStride = iter.suggestStride(maxRank);
    bool useCommonStride = iter.isStride(maxRank,commonStride)
        && expr.isStride(maxRank,commonStride);
#else
    diffType commonStride = 1;
    bool useCommonStride = false;
#endif

    int lastLength = dest.length(maxRank);

    for (int i=0; i < order.length(); ++i)
    {
        iter.pop(0);
        expr.pop(0);

#ifdef BZ_DEBUG_TRAVERSE
    cerr << "Traversing: " << order[i] << endl;
#endif
        // Position the iterator at the start of the next column       
        for (int j=1; j < N_rank; ++j)
        {
            iter.loadStride(ordering(j));
            expr.loadStride(ordering(j));

            int offset = order[i][j-1];
            iter.advance(offset);
            expr.advance(offset);
        }

        iter.loadStride(maxRank);
        expr.loadStride(maxRank);

        // Evaluate the expression along the column

        if ((useUnitStride) || (useCommonStride))
        {
#ifdef BZ_USE_FAST_READ_ARRAY_EXPR
            diffType ubound = lastLength * commonStride;
            T_numtype* _bz_restrict data = const_cast<T_numtype*>(iter.data());

            if (commonStride == 1)
            {            
 #ifndef BZ_ARRAY_FAST_TRAVERSAL_UNROLL
                for (diffType i=0; i < ubound; ++i)
                    T_update::update(*data++, expr.fastRead(i));
 #else
                diffType n1 = ubound & 3;
                diffType i=0;
                for (; i < n1; ++i)
                    T_update::update(*data++, expr.fastRead(i));

                for (; i < ubound; i += 4)
                {
                    T_update::update(*data++, expr.fastRead(i));
                    T_update::update(*data++, expr.fastRead(i+1));
                    T_update::update(*data++, expr.fastRead(i+2));
                    T_update::update(*data++, expr.fastRead(i+3));
                }
 #endif  // BZ_ARRAY_FAST_TRAVERSAL_UNROLL
            }
 #ifdef BZ_ARRAY_EXPR_USE_COMMON_STRIDE
            else {
                for (diffType i=0; i < ubound; i += commonStride)
                    T_update::update(data[i], expr.fastRead(i));
            }
 #endif // BZ_ARRAY_EXPR_USE_COMMON_STRIDE

            iter.advance(lastLength * commonStride);
            expr.advance(lastLength * commonStride);
#else   // ! BZ_USE_FAST_READ_ARRAY_EXPR
            T_numtype* _bz_restrict last = const_cast<T_numtype*>(iter.data()) 
                + lastLength * commonStride;

            while (iter.data() != last)
            {
                T_update::update(*const_cast<T_numtype*>(iter.data()), *expr);
                iter.advance(commonStride);
                expr.advance(commonStride);
            }
#endif  // BZ_USE_FAST_READ_ARRAY_EXPR

        }
        else {
            // No common stride

            T_numtype* _bz_restrict last = const_cast<T_numtype*>(iter.data()) 
                + lastLength * stride(maxRank);

            while (iter.data() != last)
            {
                T_update::update(*const_cast<T_numtype*>(iter.data()), *expr);
                iter.advance();
                expr.advance();
            }
        }
    }
}

#endif // BZ_ARRAY_SPACE_FILLING_TRAVERSAL
#endif // BZ_HAVE_STD

#ifdef BZ_ARRAY_2D_NEW_STENCIL_TILING

#ifdef BZ_ARRAY_2D_STENCIL_TILING

// what is diff between new and old?
template<typename T_dest, typename T_expr, typename T_update>
_bz_forceinline void
_bz_evaluateWithTiled2DTraversal(T_dest& dest, T_expr expr, T_update)
{
  typedef typename T_dest::T_numtype T_numtype;
  const int N_rank = T_dest::rank();

  typename T_dest::T_iterator iter(dest);

    const int minorRank = iter.ordering(0);
    const int majorRank = iter.ordering(1);

    iter.push(0);
    expr.push(0);

#ifdef BZ_2D_STENCIL_DEBUG
    int count = 0;
#endif

    bool useUnitStride = iter.isUnitStride(minorRank)
                          && expr.isUnitStride(minorRank);

#ifdef BZ_ARRAY_EXPR_USE_COMMON_STRIDE
    diffType commonStride = expr.suggestStride(minorRank);
    if (iter.suggestStride(minorRank) > commonStride)
        commonStride = iter.suggestStride(minorRank);
    bool useCommonStride = iter.isStride(minorRank,commonStride)
        && expr.isStride(minorRank,commonStride);
#else
    diffType commonStride = 1;
    bool useCommonStride = false;
#endif

    // Determine if a common major stride exists
    diffType commonMajorStride = expr.suggestStride(majorRank);
    if (iter.suggestStride(majorRank) > commonMajorStride)
        commonMajorStride = iter.suggestStride(majorRank);
    bool haveCommonMajorStride = iter.isStride(majorRank,commonMajorStride)
        && expr.isStride(majorRank,commonMajorStride);


    int maxi = dest.length(majorRank);
    int maxj = dest.length(minorRank);

    const int tileHeight = 16, tileWidth = 3;

    int bi, bj;
    for (bi=0; bi < maxi; bi += tileHeight)
    {
        int ni = bi + tileHeight;
        if (ni > maxi)
            ni = maxi;

        // Move back to the beginning of the array
        iter.pop(0);
        expr.pop(0);

        // Move to the start of this tile row
        iter.loadStride(majorRank);
        iter.advance(bi);
        expr.loadStride(majorRank);
        expr.advance(bi);

        // Save this position
        iter.push(1);
        expr.push(1);

        for (bj=0; bj < maxj; bj += tileWidth)
        {
            // Move to the beginning of the tile row
            iter.pop(1);
            expr.pop(1);

            // Move to the top of the current tile (bi,bj)
            iter.loadStride(minorRank);
            iter.advance(bj);
            expr.loadStride(minorRank);
            expr.advance(bj);

            if (bj + tileWidth <= maxj)
            {
                // Strip mining

                if ((useUnitStride) && (haveCommonMajorStride))
                {
                    diffType offset = 0;
                    T_numtype* _bz_restrict data = const_cast<T_numtype*>
                        (iter.data());

                    for (int i=bi; i < ni; ++i)
                    {
                        _bz_typename T_expr::T_numtype tmp1, tmp2, tmp3;

                        // Common subexpression elimination -- compilers
                        // won't necessarily do this on their own.
                        diffType t1 = offset+1;
                        diffType t2 = offset+2;

                        tmp1 = expr.fastRead(offset);
                        tmp2 = expr.fastRead(t1);
                        tmp3 = expr.fastRead(t2);

                        T_update::update(data[0], tmp1);
                        T_update::update(data[1], tmp2);
                        T_update::update(data[2], tmp3);

                        offset += commonMajorStride;
                        data += commonMajorStride;

#ifdef BZ_2D_STENCIL_DEBUG
    count += 3;
#endif
                    }
                }
                else {

                    for (int i=bi; i < ni; ++i)
                    {
                        iter.loadStride(minorRank);
                        expr.loadStride(minorRank);

                        // Loop through current row elements
                        T_update::update(*const_cast<T_numtype*>(iter.data()),
                            *expr);
                        iter.advance();
                        expr.advance();

                        T_update::update(*const_cast<T_numtype*>(iter.data()),
                            *expr);
                        iter.advance();
                        expr.advance();

                        T_update::update(*const_cast<T_numtype*>(iter.data()),
                            *expr);
                        iter.advance(-2);
                        expr.advance(-2);

                        iter.loadStride(majorRank);
                        expr.loadStride(majorRank);
                        iter.advance();
                        expr.advance();

#ifdef BZ_2D_STENCIL_DEBUG
    count += 3;
#endif

                    }
                }
            }
            else {

                // This code handles partial tiles at the bottom of the
                // array.

                for (int j=bj; j < maxj; ++j)
                {
                    iter.loadStride(majorRank);
                    expr.loadStride(majorRank);

                    for (int i=bi; i < ni; ++i)
                    {
                        T_update::update(*const_cast<T_numtype*>(iter.data()),
                            *expr);
                        iter.advance();
                        expr.advance();
#ifdef BZ_2D_STENCIL_DEBUG
    ++count;
#endif

                    }

                    // Move back to the top of this column
                    iter.advance(bi-ni);
                    expr.advance(bi-ni);

                    // Move over to the next column
                    iter.loadStride(minorRank);
                    expr.loadStride(minorRank);

                    iter.advance();
                    expr.advance();
                }
            }
        }
    }

#ifdef BZ_2D_STENCIL_DEBUG
    cout << "BZ_2D_STENCIL_DEBUG: count = " << count << endl;
#endif
}

#endif // BZ_ARRAY_2D_STENCIL_TILING
#endif // BZ_ARRAY_2D_NEW_STENCIL_TILING



#ifndef BZ_ARRAY_2D_NEW_STENCIL_TILING

#ifdef BZ_ARRAY_2D_STENCIL_TILING

// what is diff between new and old?
template<typename T_dest, typename T_expr, typename T_update>
_bz_forceinline void
_bz_evaluateWithTiled2DTraversal(T_dest& dest, T_expr expr, T_update)
{
  typedef typename T_dest::T_numtype T_numtype;

    typename T_dest::T_iterator iter(dest);

    const int minorRank = iter.ordering(0);
    const int majorRank = iter.ordering(1);

    const int blockSize = 16;
    
    iter.push(0);
    expr.push(0);

    bool useUnitStride = iter.isUnitStride(minorRank)
                          && expr.isUnitStride(minorRank);

#ifdef BZ_ARRAY_EXPR_USE_COMMON_STRIDE
    diffType commonStride = expr.suggestStride(minorRank);
    if (iter.suggestStride(minorRank) > commonStride)
        commonStride = iter.suggestStride(minorRank);
    bool useCommonStride = iter.isStride(minorRank,commonStride)
        && expr.isStride(minorRank,commonStride);
#else
    diffType commonStride = 1;
    bool useCommonStride = false;
#endif

    int maxi = dest.length(majorRank);
    int maxj = dest.length(minorRank);

    int bi, bj;
    for (bi=0; bi < maxi; bi += blockSize)
    {
        int ni = bi + blockSize;
        if (ni > maxi)
            ni = maxi;

        for (bj=0; bj < maxj; bj += blockSize)
        {
            int nj = bj + blockSize;
            if (nj > maxj)
                nj = maxj;

            // Move to the beginning of the array
            iter.pop(0);
            expr.pop(0);

            // Move to the beginning of the tile (bi,bj)
            iter.loadStride(majorRank);
            iter.advance(bi);
            iter.loadStride(minorRank);
            iter.advance(bj);

            expr.loadStride(majorRank);
            expr.advance(bi);
            expr.loadStride(minorRank);
            expr.advance(bj);

            // Loop through tile rows
            for (int i=bi; i < ni; ++i)
            {
                // Save the beginning of this tile row
                iter.push(1);
                expr.push(1);

                // Load the minor stride
                iter.loadStride(minorRank);
                expr.loadStride(minorRank);

                if (useUnitStride)
                {
                    T_numtype* _bz_restrict data = const_cast<T_numtype*>
                        (iter.data());

                    int ubound = (nj-bj);
                    for (int j=0; j < ubound; ++j)
                        T_update::update(*data++, expr.fastRead(j));
                }
#ifdef BZ_ARRAY_EXPR_USE_COMMON_STRIDE
                else if (useCommonStride)
                {
                    const diffType ubound = (nj-bj) * commonStride;
                    T_numtype* _bz_restrict data = const_cast<T_numtype*>
                        (iter.data());

                    for (diffType j=0; j < ubound; j += commonStride)
                        T_update::update(data[j], expr.fastRead(j));
                }
#endif
                else {
                    for (int j=bj; j < nj; ++j)
                    {
                        // Loop through current row elements
                        T_update::update(*const_cast<T_numtype*>(iter.data()), 
					 *expr);
                        iter.advance();
                        expr.advance();
                    }
                }

                // Move back to the beginning of the tile row, then
                // move to the next row
                iter.pop(1);
                iter.loadStride(majorRank);
                iter.advance(1);

                expr.pop(1);
                expr.loadStride(majorRank);
                expr.advance(1);
            }
        }
    }
}
#endif // BZ_ARRAY_2D_STENCIL_TILING
#endif // BZ_ARRAY_2D_NEW_STENCIL_TILING

BZ_NAMESPACE_END

#endif // BZ_ARRAYEVAL_CC

