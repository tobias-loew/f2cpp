// -*- C++ -*-
/***************************************************************************
 * blitz/tinyvec.h      Declaration of the IndexingVector<T, N> class
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

#ifndef BZ_INDEXINGVEC_H
#define BZ_INDEXINGVEC_H

#include <memory>
#include <blitz/blitz.h>
#include <blitz/listinit.h>
#include <blitz/et-forward.h>
#include <blitz/etbase.h>
#include <blitz/simdtypes.h>
#include <blitz/array/slice.h>

#ifdef BZ_HAVE_BOOST_SERIALIZATION
#include <boost/serialization/serialization.hpp>
#endif
#ifdef BZ_HAVE_BOOST_MPI
#include <boost/mpi/datatype.hpp>
#endif

#ifdef BZ_HAVE_CSTRING
#include <cstring> // For memcpy
#endif

namespace blitz{
    template<typename T>
    class index_wrap;
    template<typename T, typename E>
    class index_wrap_copy;

    template<int s> struct Wow;
    template<> struct Wow<1>{};


    template<typename P_functor, typename P_expr, typename P_result>
    class _bz_IndexingExpr
        : public ETBase<_bz_IndexingExpr<P_functor, P_expr, P_result>>
    {
    public:
        typedef P_functor T_functor;
        typedef P_expr T_expr;
        typedef _bz_typename T_expr::T_numtype T_numtype1;
        typedef P_result T_numtype;

        // select return type
        typedef typename unwrapET<typename T_expr::T_result>::T_unwrapped test;
        typedef typename selectET<typename T_expr::T_typeprop,
            T_numtype,
            _bz_IndexingExpr<T_functor,
            test,
            P_result> >::T_selected T_typeprop;
        typedef typename unwrapET<T_typeprop>::T_unwrapped T_result;
        typedef T_numtype T_optype;

        typedef T_expr    T_ctorArg1;
        typedef int       T_ctorArg2;    // dummy
        typedef int       T_ctorArg3;    // dummy
        typedef _bz_IndexingExpr<P_functor, _bz_typename P_expr::T_range_result,
            P_result> T_range_result;

        static constexpr int
            numArrayOperands = T_expr::numArrayOperands,
            numTVOperands = T_expr::numTVOperands,
            numTMOperands = T_expr::numTMOperands,
            numIndexPlaceholders = T_expr::numIndexPlaceholders,
            minWidth = T_expr::minWidth,
            maxWidth = T_expr::maxWidth,
            rank_ = T_expr::rank_;

        template<int N> struct tvresult {
            typedef _bz_IndexingExpr<
                T_functor,
                typename T_expr::template tvresult<N>::Type,
                T_numtype> Type;
        };

        template<typename T>
        _bz_IndexingExpr(const index_wrap<T>& w)
            : f_(w.get_iter().f_)
            , outer_iter_(w.get_iter().outer_iter_)
        { }

        template<typename T, typename E>
        _bz_IndexingExpr(const index_wrap_copy<T, E>& w)
            : f_(w.get_iter().f_)
            , outer_iter_(w.get_iter().outer_iter_)
        { }

        //template<typename E>
        //_bz_IndexingExpr(E& a)
        //{
        //    int = 42;
        //    //_bz_evaluate(*this, _bz_typename asExpr<T_expr>::T_expr(expr.unwrap()),
        //    //    _bz_update<T_numtype,
        //    //    _bz_typename asExpr<T_expr>::T_expr::T_result>());
        //    //return *this;
        //}
        _bz_IndexingExpr(const _bz_IndexingExpr& a)
            : f_(a.f_), outer_iter_(a.outer_iter_)
        { }



        //_bz_IndexingExpr(const _bz_IndexingExpr<P_functor, P_expr, P_result>& a)
        //    : f_(a.f_), outer_iter_(a.outer_iter_)
        //{ }

        _bz_IndexingExpr(BZ_ETPARM(T_functor) f, BZ_ETPARM(T_expr) a)
            : f_(f), outer_iter_(a)
        { }

        // this is identical to the above constructor
        //_bz_IndexingExpr(BZ_ETPARM(T_functor) f, _bz_typename T_expr::T_ctorArg1 a)
        //: f_(f), outer_iter_(a)  { }

#if BZ_TEMPLATE_CTOR_DOESNT_CAUSE_HAVOC
        template<typename T1>
        explicit _bz_IndexingExpr(BZ_ETPARM(T_functor) f, BZ_ETPARM(T1) a)
            : f_(f), outer_iter_(a)
        { }
#endif

        /* Functions for reading data. Because they must depend on the
        * result type, they utilize a helper class.
        */

        // For numtypes, apply operator
        template<typename T> struct readHelper {
            static T_result fastRead(const T_functor& f, const T_expr& iter,
                diffType i)
            {
                i = f.inner_iter.fastRead(i);
                i -= iter.lbound(0);
                return iter.fastRead(i);
            };
            static T_result indexop(const T_functor& f, const T_expr& iter, int i) 
                ;// TODO: implement if needed
            //{
            //    i = f.inner_iter(i);
            //    i -= iter.lbound(0);
            //    return iter[i];
            //};
            static T_result deref(const T_functor& f, const T_expr& iter) {
                int i = *f.inner_iter; //ToDo
                i -= iter.lbound(0);// iter_.array().zeroOffset();
                return iter.fastRead(i);
            }
            static T_result first_value(const T_functor& f, const T_expr& iter)  {
                int i = f.inner_iter.first_value(); //ToDo
                i -= iter.lbound(0);// iter_.array().zeroOffset();
                return iter.fastRead(i);
                //return iter.first_value();
            }
            static T_result shift(const T_functor& f, const T_expr& iter,
                int offset, int dim);
            // TODO: implement if needed
            //{
            //    return iter.shift(offset, dim);
            //}
            static T_result shift(const T_functor& f, const T_expr& iter,
                int offset1, int dim1, int offset2, int dim2);
            // TODO: implement if needed
            //{
            //    return iter.shift(offset1, dim1, offset2, dim2);
            //}
            template<int N_rank>
#ifdef BZ_ARRAY_EXPR_PASS_INDEX_BY_VALUE
            static T_result indexop(const T_functor& f, const T_expr& iter,
                const TinyVector<int, N_rank> i) {
#else
            static T_result indexop(const T_functor& f, const T_expr& iter,
                const TinyVector<int, N_rank>& i) {
#endif
                auto j = f.inner_iter(i);
                //j -= iter.lbound(0);
                return iter(j);
            }
            };

        // For ET types, bypass operator and create expression
        template<typename T> struct readHelper<ETBase<T> > {
            static T_result fastRead(const T_functor& f, const T_expr& iter,
                diffType i) 
                ;// TODO: implement if needed
            //{
            //    i = f.inner_iter(i);
            //    i -= iter.lbound(0);// iter_.array().zeroOffset();
            //    return T_result(f, iter.fastRead(i));
            //};
            static T_result indexop(const T_functor& f, const T_expr& iter, int i) 
                ;// TODO: implement if needed
            //{
            //    i = f.inner_iter(i);
            //    i -= iter.lbound(0);// iter_.array().zeroOffset();
            //    return T_result(f, iter[i]);
            //};
            static T_result deref(const T_functor& f, const T_expr& iter) 
                ;// TODO: implement if needed
   //         {
   //             int i = *f.inner_iter; //ToDo
   //             i -= iter.lbound(0);// iter_.array().zeroOffset();
   //             return iter.fastRead(i);
   ////             return T_result(f, *iter);
   //         }
            static T_result first_value(const T_functor& f, const T_expr& iter)  
                ;// TODO: implement if needed
            //{
            //    return iter.first_value();
            //}
            static T_result shift(const T_functor& f, const T_expr& iter,
                int offset, int dim) 
                ;// TODO: implement if needed
            //{
            //    return T_result(f, iter.shift(offset, dim));
            //}
            static T_result shift(const T_functor& f, const T_expr& iter,
                int offset1, int dim1, int offset2, int dim2) 
                ;// TODO: implement if needed
            //{
            //    return T_result(f, iter.shift(offset1, dim1, offset2, dim2));
            //}
            template<int N_rank>
#ifdef BZ_ARRAY_EXPR_PASS_INDEX_BY_VALUE
            static T_result indexop(const T_functor& f, const T_expr& iter,
                const TinyVector<int, N_rank> i) 
#else
            static T_result indexop(const T_functor& f, const T_expr& iter,
                const TinyVector<int, N_rank>& i) 
#endif
                ;// TODO: implement if needed
            //{
            //    i = f.inner_iter(i);
            //    return T_result(f, iter(i));
            //}
            };

        T_result fastRead(diffType i) const {
            return readHelper<T_typeprop>::fastRead(f_, outer_iter_, i);
        }

        template<int N>
        typename tvresult<N>::Type fastRead_tv(diffType i) const
        {
            i = f_.inner_iter(static_cast<_bz__int>(i));
            return typename tvresult<N>::Type(f_, outer_iter_.template fastRead_tv<N>(i));
        }

        T_result operator[](int i) const {
            return readHelper<T_typeprop>::indexop(f_, iter_, i);
        }

        template<int N_rank>
#ifdef BZ_ARRAY_EXPR_PASS_INDEX_BY_VALUE
        T_result operator()(const TinyVector<int, N_rank> i) const {
#else
        T_result operator()(const TinyVector<int, N_rank>& i) const {
#endif
            return readHelper<T_typeprop>::indexop(f_, outer_iter_, i);
        }

        T_result operator*() const {
            return readHelper<T_typeprop>::deref(f_, outer_iter_);
        }

        T_result first_value() const {
            return readHelper<T_typeprop>::first_value(f_, iter_);
        }

        T_result shift(int offset, int dim) const {
            return readHelper<T_typeprop>::shift(f_, iter_, offset, dim);
        }

        T_result shift(int offset1, int dim1, int offset2, int dim2) const {
            return readHelper<T_typeprop>::shift(f_, iter_, offset1, dim1,
                offset2, dim2);
        }

        // ****** end reading

        //sizeType numElements();
        //sizeType                               numElements() const {
        //    return f_.inner_iter.get_iter().numElements();
        //}

        //auto shape() const{
        //    return f_.inner_iter.get_iter().length();
        //}
        int                               length(int rank) const{
     bla bla       return f_.inner_iter.length(rank);
     return f_.inner_iter.get_iter().length(rank);
        }
        diffType                               stride(int rank)
        {
            f_.inner_iter.get_iter().loadStride(rank);
            return f_.inner_iter.get_iter().stride();
        }
        typename T_numtype * data(){
            int i = *f_.inner_iter; //ToDo
            i -= outer_iter_.lbound(0);// iter_.array().zeroOffset();
            i *= outer_iter_.get_iter().stride();
            auto&& data = outer_iter_.get_iter().data_addr();
            return &(data[i]);



            //return &outer_iter_(i);
            //            return nullptr;
        }

        //            void loadStride(int rank);


        bool isVectorAligned(diffType offset) const
        {
            return false;
            //                return iter_.isVectorAligned(offset);
        }

        T_range_result operator()(RectDomain<rank_> d) const
        {
            return T_range_result(f_, iter_(d));
        }

        int ascending(const int rank) const {
            return iter_.ascending(rank);
        }
        int ordering(const int rank)  const {
            return f_.inner_iter.ordering(rank);
        }
        int lbound(const int rank)    const {
            return f_.inner_iter.lbound(rank);
        }
        int ubound(const int rank)    const {
            return f_.inner_iter.ubound(rank);
        }
        RectDomain<rank_> domain() const {
            return iter_.domain();
        }

        void push(const int position) {
            f_.inner_iter.push(position);
        }

        void pop(const int position)  {
            f_.inner_iter.pop(position);
        }

        void advance() {
            f_.inner_iter.advance();
        }

        void advance(const int n) {
            f_.inner_iter.advance(n);
        }

        void loadStride(const int rank) {
            outer_iter_.loadStride(rank);
            f_.inner_iter.loadStride(rank);
            //iter_.loadStride(rank);
        }

        constexpr bool isUnitStride(const int rank) const {
            return false;
            //return iter_.isUnitStride(rank);
        }

        constexpr bool isUnitStride() const {
            return false;
            //return iter_.isUnitStride();
        }

        void advanceUnitStride() {
            iter_.advanceUnitStride();
        }

        bool canCollapse(const int outerLoopRank, const int innerLoopRank) const
        {
            return iter_.canCollapse(outerLoopRank, innerLoopRank);
        }

        // this is needed for the stencil expression fastRead to work
        void _bz_offsetData(sizeType i)
        {
            iter_._bz_offsetData(i);
        }

        // and these are needed for stencil expression shift to work
        void _bz_offsetData(sizeType offset, int dim)
        {
            iter_._bz_offsetData(offset, dim);
        }

        void _bz_offsetData(sizeType offset1, int dim1, sizeType offset2, int dim2)
        {
            iter_._bz_offsetData(offset1, dim1, offset2, dim2);
        }

        diffType suggestStride(const int rank) const
        {
            return 1;
            //return iter_.suggestStride(rank);
        }

        bool isStride(const int rank, const diffType stride) const
        {
            return false;
            //                return iter_.isStride(rank, stride);
        }

        void prettyPrint(BZ_STD_SCOPE(string) &str,
            prettyPrintFormat& format) const
        {
            str += BZ_DEBUG_TEMPLATE_AS_STRING_LITERAL(T_functor);
            str += "(";
            f_.inner_iter.prettyPrint(str, format);
//            iter_.prettyPrint(str, format);
            str += ")";
        }

        template<typename T_shape>
        bool shapeCheck(const T_shape& shape) const
        {
            return f_.inner_iter.shapeCheck(shape);
        }

        template<int N_rank>
        void moveTo(const TinyVector<int, N_rank>& i)
        {
            iter_.moveTo(i);
        }

        // sliceinfo for expressions
        template<typename T1, typename T2 = nilArraySection,
            class T3 = nilArraySection, typename T4 = nilArraySection,
            class T5 = nilArraySection, typename T6 = nilArraySection,
            class T7 = nilArraySection, typename T8 = nilArraySection,
            class T9 = nilArraySection, typename T10 = nilArraySection,
            class T11 = nilArraySection>
            class SliceInfo {
            public:
                typedef typename T_expr::template SliceInfo<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>::T_slice T_slice1;
                typedef _bz_IndexingExpr<T_functor, T_slice1, T_numtype> T_slice;
        };

        template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6,
            typename T7, typename T8, typename T9, typename T10, typename T11>
            typename SliceInfo<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>::T_slice
            operator()(T1 r1, T2 r2, T3 r3, T4 r4, T5 r5, T6 r6, T7 r7, T8 r8, T9 r9, T10 r10, T11 r11) const
        {
            return typename SliceInfo<T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11>::T_slice
            (f_, iter_(r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11));
        }




    protected:
        _bz_IndexingExpr() { }

        T_functor f_;
        T_expr outer_iter_;
        };


    template<int N> struct _bz_evaluator_indexed {
        template<typename T_dest, typename T_expr, typename T_update>
        static void evaluateWithIndexTraversal(T_dest&, T_expr, T_update);
    };

    template<> struct _bz_evaluator_indexed<1> {
        template<typename T_dest, typename T_expr, typename T_update>
        static void evaluateWithIndexTraversal(T_dest&, T_expr, T_update);
    };


    template<typename T_dest, typename T_expr, typename T_update>
    _bz_forceinline void
        _bz_evaluator_indexed<1>::
        evaluateWithIndexTraversal(T_dest& dest, T_expr expr, T_update)
    {
        typedef typename T_dest::T_numtype T_numtype;

        TinyVector<int, T_dest::rank_> index;

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

    template<int N>
    template<typename T_dest, typename T_expr, typename T_update>
    _bz_forceinline void
        _bz_evaluator_indexed<N>::
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
        for(int i = 1; i < N_rank; ++i)
            iter.push(iter.ordering(i));

        iter.loadStride(maxRank);

        TinyVector<int, T_dest::rank_> index, last;

        index = dest.base();

        for(int i = 0; i < N_rank; ++i)
            last(i) = dest.base(i) + dest.length(i);

        // int lastLength = length(maxRank);

        while(true) {

            for(index[maxRank] = dest.base(maxRank);
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
            for(; j < N_rank; ++j)
            {
                iter.pop(dest.ordering(j));
                iter.loadStride(dest.ordering(j));
                iter.advance();

                index[dest.ordering(j - 1)] = dest.base(dest.ordering(j - 1));
                ++index[dest.ordering(j)];
                if(index[dest.ordering(j)] != last[dest.ordering(j)])
                    break;
            }

            if(j == N_rank)
                break;

            for(; j > 0; --j)
            {
                iter.push(dest.ordering(j));
            }
            iter.loadStride(maxRank);
        }
    }


    template<typename T_dest, typename T_expr, typename T_update>
    _bz_forceinline void
        _bz_evaluate_indexed(T_dest& dest, T_expr expr, T_update)
    {
        typedef typename T_dest::T_numtype T_numtype;
        const int N_rank = T_dest::rank_;

        // Check that all arrays have the same shape
#ifdef BZ_DEBUG
//        if(!expr.shapeCheck(dest.shape()))
//        {
//            if(assertFailMode == false)
//            {
//                cerr << "[Blitz++] Shape check failed: Module " << __FILE__
//                    << " line " << __LINE__ << endl
//                    << "          Expression: ";
//                prettyPrintFormat format(true);   // Use terse formatting
//                BZ_STD_SCOPE(string) str;
//                expr.prettyPrint(str, format);
//                cerr << str << endl;
//            }
//
//#if 0
//            // Shape dumping is broken by change to using string for prettyPrint
//            << "          Shapes: " << shape() << " = ";
//            prettyPrintFormat format2;
//            format2.setDumpArrayShapesMode();
//            expr.prettyPrint(cerr, format2);
//            cerr << endl;
//#endif
//            BZ_PRE_FAIL;
//        }
#endif

        //BZPRECHECK(expr.shapeCheck(dest.shape()),
        //    "Shape check failed." << endl << "Expression:");

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


        Wow<T_dest::rank_> test;// == 1, "T_dest::rank_ == 1");
        test;

        _bz_evaluator_indexed<T_dest::rank_>::evaluateWithIndexTraversal(dest, expr, T_update());
    }


    template<typename T>
    class index_wrap : public T
    {
    public:
        using T_iterator = typename T::T_expr;


        index_wrap(T const& t) : T(t)
        {}

        index_wrap(const index_wrap& c) : T(c)
        {
        }

        typename T::T_expr const & get_iter() const {
            return iter_;
        }


        template<typename T_expr> index_wrap& operator=(const ETBase<T_expr>& expr)
        {
            _bz_evaluate_indexed(*this, _bz_typename asExpr<T_expr>::T_expr(expr.unwrap()),
                _bz_update<T_numtype,
                _bz_typename asExpr<T_expr>::T_expr::T_result>());
            return *this;
        }

        index_wrap& operator=(const index_wrap& expr)
        {
            _bz_evaluate_indexed(*this,
                expr,
                //                _bz_typename asExpr<T_expr>::T_expr(expr.unwrap()),
                _bz_update<T_numtype,
                T_numtype
                //                _bz_typename asExpr<T_expr>::T_expr::T_result
                >());
            return *this;
            //_bz_evaluate_indexed(*this, expr,
            //    _bz_update<T_numtype,
            //    _bz_typename asExpr<T_iterator>::T_expr::T_result>());
            //return *this;
        }

        //{
        //    _bz_evaluate_indexed(*this, _bz_typename asExpr<T_expr>::T_expr(expr.unwrap()),
        //        _bz_update<T_numtype,
        //        _bz_typename asExpr<T_expr>::T_expr::T_result>());
        //    return *this;
        //}

        // Scalar operand
        index_wrap& operator=(typename T::T_numtype x)
        {
            return *this = asExpr<T_numtype>::getExpr(x);
        }


        int                               length(int rank) const{
            return iter_.length(rank);
        }
        diffType                               stride(int rank)
        {
            return iter_.stride(rank);
        }
        typename T::T_numtype * data(){
            return iter_.data();
        }


        auto shape() const {
            return iter_.shape();
        }
    };




    template<typename T>
    auto make_index_wrap(T const& t){
        return blitz::index_wrap<T>(t);
    }



    template<typename T, typename E>
    class index_wrap_copy : public T
    {
    public:
        using T_iterator = typename T::T_expr;

        std::remove_reference_t<E> e;


        index_wrap_copy(T const& t, E e) : T(t), e(e)
        {}

        index_wrap_copy(const index_wrap_copy& c) : T(c), e(c.e)
        {
        }


        typename T::T_expr const & get_iter() const {
            return iter_;
        }


        template<typename T_expr> index_wrap_copy& operator=(const ETBase<T_expr>& expr)
        {
            _bz_evaluate_indexed(*this, _bz_typename asExpr<T_expr>::T_expr(expr.unwrap()),
                _bz_update<T_numtype,
                _bz_typename asExpr<T_expr>::T_expr::T_result>());
            return *this;
        }

        index_wrap_copy& operator=(const index_wrap_copy& expr)
        {
            _bz_evaluate_indexed(*this,
                expr,
                //                _bz_typename asExpr<T_expr>::T_expr(expr.unwrap()),
                _bz_update<T_numtype,
                T_numtype
                //                _bz_typename asExpr<T_expr>::T_expr::T_result
                >());
            return *this;
            //_bz_evaluate_indexed(*this, expr,
            //    _bz_update<T_numtype,
            //    _bz_typename asExpr<T_iterator>::T_expr::T_result>());
            //return *this;
        }

        //{
        //    _bz_evaluate_indexed(*this, _bz_typename asExpr<T_expr>::T_expr(expr.unwrap()),
        //        _bz_update<T_numtype,
        //        _bz_typename asExpr<T_expr>::T_expr::T_result>());
        //    return *this;
        //}

        // Scalar operand
        index_wrap_copy& operator=(typename T::T_numtype x)
        {
            return *this = asExpr<T_numtype>::getExpr(x);
        }


        int                               length(int rank) const{
            return iter_.length(rank);
        }
        diffType                               stride(int rank)
        {
            return iter_.stride(rank);
        }
        typename T::T_numtype * data(){
            return iter_.data();
        }


        auto shape() const {
            return iter_.shape();
        }
    };




    template<typename T, typename E>
    auto make_index_wrap_copy(T const& t, E&& e){
        //using E_copy = typename std::remove_cv_t<std::remove_reference_t<E>>;

        return blitz::index_wrap_copy<T, E>(t, std::forward<E>(e));
    }



    template<typename I, typename I_expr>
    class indexer_functor {
    public:

        I_expr inner_iter;
        indexer_functor(I_expr i) :inner_iter(i)
        {}


        template<typename P_expr>
        blitz::index_wrap<blitz::_bz_ArrayExpr<blitz::_bz_IndexingExpr<
            indexer_functor,
            typename blitz::asExpr<P_expr>::T_expr,
            typename blitz::asExpr<P_expr>::T_expr::T_optype> >>
            operator()(const blitz::ETBase<P_expr>& a) const
        {
            return make_index_wrap(blitz::_bz_ArrayExpr<
                blitz::_bz_IndexingExpr<indexer_functor,
                typename blitz::asExpr<P_expr>::T_expr,
                typename blitz::asExpr<P_expr>::T_expr::T_optype> >
                (*this, a.unwrap()));
        }

    };

    template<typename I, typename I_expr>
    class indexer_functor_copy {
    public:

//        std::shared_ptr<I_expr> inner_iter;
        I_expr inner_iter;
        indexer_functor_copy(I_expr i) 
            : inner_iter(i)
//            : inner_iter(new I_expr{i})
        {}


        template<typename P_expr>
        //blitz::index_wrap_copy<blitz::_bz_ArrayExpr<blitz::_bz_IndexingExpr<
        //    indexer_functor_copy,
        //    typename blitz::asExpr<P_expr>::T_expr,
        //    typename blitz::asExpr<P_expr>::T_expr::T_optype> >,
        //    P_expr
        //>
            auto
            operator()(const blitz::ETBase<P_expr>& a) const
        {
//            using P_expr = typename std::remove_cv_t<std::remove_reference_t<decltype()>>;
            auto cloned = std::make_shared<P_expr>(a.unwrap());

            return make_index_wrap_copy(blitz::_bz_ArrayExpr<
                blitz::_bz_IndexingExpr<indexer_functor_copy,
                typename blitz::asExpr<P_expr>::T_expr,
                typename blitz::asExpr<P_expr>::T_expr::T_optype> >
                (*this, *cloned), cloned);
        }


    };

    namespace aux {
        template<typename I, typename I_expr>
        auto make_indexing_functor(I_expr i){
            return indexer_functor<I, I_expr>(i);
        }
        template<typename I, typename I_expr>
        auto make_indexing_functor_copy(I_expr i){
            return indexer_functor_copy<I, I_expr>(i);
        }
    }
    template<typename I>
    auto make_indexing_functor(I& i){
        return aux::make_indexing_functor<I>(blitz::asExpr<I>::getExpr(i));
    }

    template<typename I>
    auto make_indexing_functor_copy(I& i){
        return aux::make_indexing_functor_copy<I>(blitz::asExpr<I>::getExpr(i));
    }

}


#endif // BZ_INDEXINGVEC_H

