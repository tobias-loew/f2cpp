/***************************************************************************
 * blitz/tinyvec.cc  Declaration of TinyBasedVector methods
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
 ***************************************************************************/

#ifndef BZ_TINYVEC2BASE_CC
#define BZ_TINYVEC2BASE_CC

#include <blitz/tinyvec2.h>
#include <blitz/update.h>
#include <blitz/extension/tbvevaluate.h>
#include <blitz/array/asexpr.h>

BZ_NAMESPACE(blitz)

/*
 * Constructors
 */

template<typename P_numtype, int N_length, int N_base>
_bz_forceinline
TinyBasedVector<P_numtype, N_length, N_base>::TinyBasedVector(const T_numtype initValue) {
    for (int i=0; i < N_length; ++i)
        data_[i] = initValue;
}

template<typename P_numtype, int N_length, int N_base>
_bz_forceinline 
TinyBasedVector<P_numtype, N_length, N_base>::TinyBasedVector(const TinyBasedVector<T_numtype, N_length, N_base>& x) {
    for (int i=0; i < N_length; ++i)
        data_[i] = x.data_[i];
}

template<typename P_numtype, int N_length, int N_base>
template<typename P_numtype2>
_bz_forceinline 
TinyBasedVector<P_numtype, N_length, N_base>::TinyBasedVector(const TinyBasedVector<P_numtype2, N_length, N_base>& x) {
    for (int i=0; i < N_length; ++i)
        data_[i] = static_cast<P_numtype>(x[i]);
}


/*
 * Assignment-type operators
 */

template<typename P_numtype, int N_length, int N_base>
_bz_forceinline
TinyBasedVector<P_numtype, N_length, N_base>& 
TinyBasedVector<P_numtype,N_length, N_base>::initialize(T_numtype x)
{
    for(sizeType i = 0; i < N_length; ++i) {
        data_[i] = static_cast<P_numtype>(x);
    }
//    (*this) = _bz_ArrayExpr<_bz_ArrayExprConstant<T_numtype> >(x);
    return *this;
}

template<typename P_numtype, int N_length, int N_base> template<typename T_expr>
_bz_forceinline
TinyBasedVector<P_numtype,N_length, N_base>&
TinyBasedVector<P_numtype,N_length, N_base>::operator=(const ETBase<T_expr>& expr)
{
  _tbv_evaluate(_bz_typename asExpr<T_expr>::T_expr(expr.unwrap()), 
	       _bz_update<
	       T_numtype, 
	       _bz_typename asExpr<T_expr>::T_expr::T_result>());
    return *this;
}

#define BZ_TBV2_UPDATE(op,name)						\
  template<typename P_numtype, int N_length, int N_base>				\
  template<typename T>							\
  _bz_forceinline								\
  TinyBasedVector<P_numtype,N_length, N_base>&					\
  TinyBasedVector<P_numtype,N_length, N_base>::operator op(const T& expr)		\
  {									\
    _tbv_evaluate(_bz_typename asExpr<T>::T_expr(expr),			\
		 name<T_numtype,					\
		 _bz_typename asExpr<T>::T_expr::T_result>());		\
    return *this;							\
  }


BZ_TBV2_UPDATE(+=, _bz_plus_update)
BZ_TBV2_UPDATE(-=, _bz_minus_update)
BZ_TBV2_UPDATE(*=, _bz_multiply_update)
BZ_TBV2_UPDATE(/=, _bz_divide_update)
BZ_TBV2_UPDATE(%=, _bz_mod_update)
BZ_TBV2_UPDATE(^=, _bz_xor_update)
BZ_TBV2_UPDATE(&=, _bz_bitand_update)
BZ_TBV2_UPDATE(|=, _bz_bitor_update)
BZ_TBV2_UPDATE(<<=, _bz_shiftl_update)
BZ_TBV2_UPDATE(>>=, _bz_shiftr_update)

/*
 * Other member functions
 */

template<typename P_numtype, int N_length, int N_base>
template<int N0>
_bz_forceinline
_bz_ArrayExpr<ArrayIndexMapping<typename asExpr<TinyBasedVector<P_numtype, N_length, N_base> >::T_expr, N0> >
TinyBasedVector<P_numtype, N_length, N_base>::operator()(IndexPlaceholder<N0>) const
{ 
        return _bz_ArrayExpr<ArrayIndexMapping<typename asExpr<T_vector>::T_expr, N0> >
            (noConst());
}






BZ_NAMESPACE_END

#include <blitz/tv2fastiter.h>  // Iterators
//#include <blitz/tv2assign.h> unused now?
#include <blitz/tinyvec2io.cc>

#endif // BZ_TINYBASEDVEC_CC
