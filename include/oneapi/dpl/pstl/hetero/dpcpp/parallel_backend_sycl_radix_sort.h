// -*- C++ -*-
//===-- parallel_backend_sycl_radix_sort.h --------------------------------===//
//
// Copyright (C) Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// This file incorporates work covered by the following copyright and permission
// notice:
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#ifndef _ONEDPL_parallel_backend_sycl_radix_sort_H
#define _ONEDPL_parallel_backend_sycl_radix_sort_H

#include <limits>
#include <type_traits>
#include <utility>
#include <cstdint>

#if (__cpluslplus >= 202002L || _MSVC_LANG >= 202002L) && __has_include(<bit>)
#    include <bit>
#else
#    include <cstring> // memcpy
#endif

#include "sycl_defs.h"
#include "parallel_backend_sycl_utils.h"
#include "execution_sycl_defs.h"

namespace oneapi
{
namespace dpl
{
namespace __par_backend_hetero
{
//------------------------------------------------------------------------
// radix sort: kernel names
//------------------------------------------------------------------------

template <::std::uint32_t, bool, bool, typename... _Name>
class __radix_sort_count_kernel;

template <::std::uint32_t, typename... _Name>
class __radix_sort_scan_kernel;

template <::std::uint32_t, bool, bool, typename... _Name>
class __radix_sort_reorder_peer_kernel;

template <::std::uint32_t, bool, bool, typename... _Name>
class __radix_sort_reorder_kernel;

//------------------------------------------------------------------------
// radix sort: bitwise order-preserving conversions to unsigned integrals
//------------------------------------------------------------------------

#if (__cpluslplus >= 202002L || _MSVC_LANG >= 202002L) && __has_include(<bit>)
template <typename _Dst, typename _Src>
using __dpl_bit_cast = ::std::bit_cast<_Dst, _Src>;

#else
template <typename _Dst, typename _Src>
__enable_if_t<
    sizeof(_Dst) == sizeof(_Src) && ::std::is_trivially_copyable_v<_Dst> && ::std::is_trivially_copyable_v<_Src>, _Dst>
__dpl_bit_cast(const _Src& __src)
{
#if defined(__has_builtin) && __has_builtin(__builtin_bit_cast)
    return __builtin_bit_cast(_Dst, __src);
#else
    _Dst __result;
    ::std::memcpy(&__result, &__src, sizeof(_Dst));
    return __result;
#endif
}
#endif // C++20 && __has_include(<bit>)

template <bool __is_ascending>
bool
__order_preserving_cast(bool __val)
{
    if constexpr (__is_ascending)
        return __val;
    else
        return !__val;
}

template <bool __is_ascending, typename _UInt, __enable_if_t<::std::is_unsigned_v<_UInt>, int> = 0>
_UInt
__order_preserving_cast(_UInt __val)
{
    if constexpr (__is_ascending)
        return __val;
    else
        return ~__val; //bitwise inversion
}

template <bool __is_ascending, typename _Int,
          __enable_if_t<::std::is_integral_v<_Int> && ::std::is_signed_v<_Int>, int> = 0>
::std::make_unsigned_t<_Int>
__order_preserving_cast(_Int __val)
{
    using _UInt = ::std::make_unsigned_t<_Int>;
    // mask: 100..0 for ascending, 011..1 for descending
    constexpr _UInt __mask = (__is_ascending) ? 1 << ::std::numeric_limits<_Int>::digits : ~_UInt(0) >> 1;
    return __val ^ __mask;
}

template <bool __is_ascending, typename _Float,
          __enable_if_t<::std::is_floating_point_v<_Float> && sizeof(_Float) == sizeof(::std::uint32_t), int> = 0>
::std::uint32_t
__order_preserving_cast(_Float __val)
{
    ::std::uint32_t __uint32_val = __dpl_bit_cast<::std::uint32_t>(__val);
    ::std::uint32_t __mask;
    // __uint32_val >> 31 takes the sign bit of the original value
    if constexpr (__is_ascending)
        __mask = (__uint32_val >> 31 == 0) ? 0x80000000u : 0xFFFFFFFFu;
    else
        __mask = (__uint32_val >> 31 == 0) ? 0x7FFFFFFFu : ::std::uint32_t(0);
    return __uint32_val ^ __mask;
}

template <bool __is_ascending, typename _Float,
          __enable_if_t<::std::is_floating_point_v<_Float> && sizeof(_Float) == sizeof(::std::uint64_t), int> = 0>
::std::uint64_t
__order_preserving_cast(_Float __val)
{
    ::std::uint64_t __uint64_val = __dpl_bit_cast<::std::uint64_t>(__val);
    ::std::uint64_t __mask;
    // __uint64_val >> 63 takes the sign bit of the original value
    if constexpr (__is_ascending)
        __mask = (__uint64_val >> 63 == 0) ? 0x8000000000000000u : 0xFFFFFFFFFFFFFFFFu;
    else
        __mask = (__uint64_val >> 63 == 0) ? 0x7FFFFFFFFFFFFFFFu : ::std::uint64_t(0);
    return __uint64_val ^ __mask;
}

//------------------------------------------------------------------------
// radix sort: bit pattern functions
//------------------------------------------------------------------------

// get rounded up result of (__number / __divisor)
template <typename _T1, typename _T2>
constexpr auto
__ceiling_div(_T1 __number, _T2 __divisor) -> decltype((__number - 1) / __divisor + 1)
{
    return (__number - 1) / __divisor + 1;
}

// Use sycl::clz to implement the analogue of C++20 std::bit_floor (the max power of 2 not exceeding the value)
template <typename _T>
inline __enable_if_t<::std::is_integral<_T>::value && ::std::is_unsigned<_T>::value, _T>
__dpl_bit_floor(_T __x)
{
    return 1 << (sycl::clz(_T(0)) - sycl::clz(__x) - 1);
}

// get number of buckets (size of radix bits) in T
template <typename _T>
constexpr ::std::uint32_t
__get_buckets_in_type(::std::uint32_t __radix_bits)
{
    return __ceiling_div(sizeof(_T) * ::std::numeric_limits<unsigned char>::digits, __radix_bits);
}

// get bits value (bucket) in a certain radix position
template <::std::uint32_t __radix_mask, typename _T>
::std::uint32_t
__get_bucket(_T __value, ::std::uint32_t __radix_offset)
{
    return (__value >> __radix_offset) & _T(__radix_mask);
}

//-----------------------------------------------------------------------
// radix sort: count kernel (per iteration)
//-----------------------------------------------------------------------

template <typename _KernelName, ::std::uint32_t __radix_bits, bool __is_ascending, typename _ExecutionPolicy,
          typename _ValRange, typename _CountBuf
#if _ONEDPL_COMPILE_KERNEL
          , typename _Kernel
#endif
          >
sycl::event
__radix_sort_count_submit(_ExecutionPolicy&& __exec, ::std::size_t __segments, ::std::size_t __block_size,
                          ::std::uint32_t __radix_offset, _ValRange&& __val_rng, _CountBuf& __count_buf,
                          sycl::event __dependency_event
#if _ONEDPL_COMPILE_KERNEL
                          , _Kernel& __kernel
#endif
)
{
    // typedefs
    using _ValueT = oneapi::dpl::__internal::__value_t<_ValRange>;
    using _CountT = typename _CountBuf::value_type;

    // radix states used for an array storing bucket state counters
    constexpr ::std::uint32_t __radix_states = 1 << __radix_bits;

    const ::std::size_t __val_buf_size = __val_rng.size();
    // iteration space info
    const ::std::size_t __blocks_total = __ceiling_div(__val_buf_size, __block_size);
    const ::std::size_t __blocks_per_segment = __ceiling_div(__blocks_total, __segments);

    auto __count_rng =
        oneapi::dpl::__ranges::all_view<_CountT, __par_backend_hetero::access_mode::read_write>(__count_buf);

    // submit to compute arrays with local count values
    sycl::event __count_levent = __exec.queue().submit([&](sycl::handler& __hdl) {
        __hdl.depends_on(__dependency_event);

        oneapi::dpl::__ranges::__require_access(__hdl, __val_rng,
                                                __count_rng); //get an access to data under SYCL buffer
        // an accessor per work-group with value counters from each work-item
        auto __count_lacc = sycl::accessor<_CountT, 1, access_mode::read_write, __dpl_sycl::__target::local>(
            __block_size * __radix_states, __hdl);
#if _ONEDPL_COMPILE_KERNEL && _ONEDPL_KERNEL_BUNDLE_PRESENT
        __hdl.use_kernel_bundle(__kernel.get_kernel_bundle());
#endif
        __hdl.parallel_for<_KernelName>(
#if _ONEDPL_COMPILE_KERNEL && !_ONEDPL_KERNEL_BUNDLE_PRESENT
            __kernel,
#endif
            sycl::nd_range<1>(__segments * __block_size, __block_size), [=](sycl::nd_item<1> __self_item) {
                // item info
                const ::std::size_t __self_lidx = __self_item.get_local_id(0);
                const ::std::size_t __wgroup_idx = __self_item.get_group(0);
                const ::std::size_t __start_idx = __blocks_per_segment * __block_size * __wgroup_idx + __self_lidx;

                // 1.1. count per witem: create a private array for storing count values
                _CountT __count_arr[__radix_states] = {0};
                // 1.2. count per witem: count values and write result to private count array
                const ::std::size_t __outside_of_segment =
                    sycl::min(__start_idx + __block_size * __blocks_per_segment, __val_buf_size);
                for (::std::size_t __val_idx = __start_idx; __val_idx < __outside_of_segment; __val_idx += __block_size)
                {
                    // get the bucket for the bit-ordered input value, applying the offset and mask for radix bits
                    auto __val = __order_preserving_cast<__is_ascending>(__val_rng[__val_idx]);
                    ::std::uint32_t __bucket = __get_bucket<(1 << __radix_bits) - 1>(__val, __radix_offset);
                    // increment counter for this bit bucket
                    ++__count_arr[__bucket];
                }
                // 1.3. count per witem: write private count array to local count array
                const ::std::uint32_t __count_start_idx = __radix_states * __self_lidx;
                for (::std::uint32_t __radix_state_idx = 0; __radix_state_idx < __radix_states; ++__radix_state_idx)
                    __count_lacc[__count_start_idx + __radix_state_idx] = __count_arr[__radix_state_idx];
                __dpl_sycl::__group_barrier(__self_item);

                // 2.1. count per wgroup: reduce till __count_lacc[] size > __block_size (all threads work)
                for (::std::uint32_t __i = 1; __i < __radix_states; ++__i)
                    __count_lacc[__self_lidx] += __count_lacc[__block_size * __i + __self_lidx];
                __dpl_sycl::__group_barrier(__self_item);
                // 2.2. count per wgroup: reduce until __count_lacc[] size > __radix_states (threads /= 2 per iteration)
                for (::std::uint32_t __active_ths = __block_size >> 1; __active_ths >= __radix_states;
                     __active_ths >>= 1)
                {
                    if (__self_lidx < __active_ths)
                        __count_lacc[__self_lidx] += __count_lacc[__active_ths + __self_lidx];
                    __dpl_sycl::__group_barrier(__self_item);
                }
                // 2.3. count per wgroup: write local count array to global count array
                if (__self_lidx < __radix_states)
                {
                    // move buckets with the same id to adjacent positions,
                    // thus splitting __count_rng into __radix_states regions
                    __count_rng[(__segments + 1) * __self_lidx + __wgroup_idx] = __count_lacc[__self_lidx];
                }
            });
    });

    return __count_levent;
}

//-----------------------------------------------------------------------
// radix sort: scan kernel (per iteration)
//-----------------------------------------------------------------------

template <typename _KernelName, ::std::uint32_t __radix_bits, typename _ExecutionPolicy, typename _CountBuf
#if _ONEDPL_COMPILE_KERNEL
          , typename _Kernel
#endif
          >
sycl::event
__radix_sort_scan_submit(_ExecutionPolicy&& __exec, ::std::size_t __scan_wg_size, ::std::size_t __segments,
                         _CountBuf& __count_buf, sycl::event __dependency_event
#if _ONEDPL_COMPILE_KERNEL
                        , _Kernel& __kernel
#endif
)
{
    using _CountT = typename _CountBuf::value_type;

    auto __count_rng =
        oneapi::dpl::__ranges::all_view<_CountT, __par_backend_hetero::access_mode::read_write>(__count_buf);
    using __count_rng_type = decltype(__count_rng);

    // Scan produces local offsets using count values.
    // There are no local offsets for the first segment, but the rest segments should be scanned
    // with respect to the count value in the first segment what requires n + 1 positions
    const ::std::size_t __scan_size = __segments + 1;
    __scan_wg_size = ::std::min(__scan_size, __scan_wg_size);

    const ::std::uint32_t __radix_states = 1 << __radix_bits;

    // compilation of the kernel prevents out of resources issue, which may occur due to usage of
    // collective algorithms such as joint_exclusive_scan even if local memory is not explicitly requested
    sycl::event __scan_event = __exec.queue().submit([&](sycl::handler& __hdl) {
        __hdl.depends_on(__dependency_event);
        // an accessor with value counter from each work_group
        oneapi::dpl::__ranges::__require_access(__hdl, __count_rng); //get an access to data under SYCL buffer
#if _ONEDPL_COMPILE_KERNEL && _ONEDPL_KERNEL_BUNDLE_PRESENT
        __hdl.use_kernel_bundle(__kernel.get_kernel_bundle());
#endif
        __hdl.parallel_for<_KernelName>(
#if _ONEDPL_COMPILE_KERNEL && !_ONEDPL_KERNEL_BUNDLE_PRESENT
            __kernel,
#endif
            sycl::nd_range<1>(__radix_states * __scan_wg_size, __scan_wg_size), [=](sycl::nd_item<1> __self_item) {
                // find borders of a region with a specific bucket id
                sycl::global_ptr<_CountT> __begin = __count_rng.begin() + __scan_size * __self_item.get_group(0);
                // TODO: consider another approach with use of local memory
                __dpl_sycl::__joint_exclusive_scan(__self_item.get_group(), __begin, __begin + __scan_size, __begin,
                                                   _CountT(0), __dpl_sycl::__plus<_CountT>{});
            });
    });
    return __scan_event;
}

struct __empty_peer_temp_storage
{
    template <typename... T>
    __empty_peer_temp_storage(T&&...)
    {
    }
};

enum class __peer_prefix_algo
{
    subgroup_ballot,
    atomic_fetch_or,
    scan_then_broadcast
};

template <typename _OffsetT, __peer_prefix_algo _Algo>
struct __peer_prefix_helper;

#if (_ONEDPL_LIBSYCL_VERSION >= 50700)
template <typename _OffsetT>
struct __peer_prefix_helper<_OffsetT, __peer_prefix_algo::atomic_fetch_or>
{
    using _AtomicT = __dpl_sycl::__atomic_ref<::std::uint32_t, sycl::access::address_space::local_space>;
    using _TempStorageT =
        sycl::accessor<::std::uint32_t, 1, sycl::access::mode::read_write, sycl::access::target::local>;

    sycl::sub_group __sgroup;
    ::std::uint32_t __self_lidx;
    ::std::uint32_t __item_mask;
    _AtomicT __atomic_peer_mask;

    __peer_prefix_helper(sycl::nd_item<1> __self_item, _TempStorageT __lacc)
        : __sgroup(__self_item.get_sub_group()), __self_lidx(__self_item.get_local_linear_id()),
          __item_mask(~(~0u << (__self_lidx))), __atomic_peer_mask(*__lacc.get_pointer())
    {
    }

    ::std::uint32_t
    __peer_contribution(_OffsetT& __new_offset_idx, _OffsetT __offset_prefix, bool __is_current_bucket)
    {
        // reset mask for each radix state
        if (__self_lidx == 0)
            __atomic_peer_mask.store(0U);
        sycl::group_barrier(__sgroup);
        // set local id's bit to 1 if the bucket value matches the radix state
        __atomic_peer_mask.fetch_or(__is_current_bucket << __self_lidx);
        sycl::group_barrier(__sgroup);
        ::std::uint32_t __peer_mask_bits = __atomic_peer_mask.load();
        ::std::uint32_t __sg_total_offset = sycl::popcount(__peer_mask_bits);

        // get the local offset index from the bits set in the peer mask with index less than the work
        // items's ID
        __peer_mask_bits &= __item_mask;
        __new_offset_idx |= __is_current_bucket * (__offset_prefix + sycl::popcount(__peer_mask_bits));
        return __sg_total_offset;
    }
};
#endif // (_ONEDPL_LIBSYCL_VERSION >= 50700)

template <typename _OffsetT>
struct __peer_prefix_helper<_OffsetT, __peer_prefix_algo::scan_then_broadcast>
{
    using _TempStorageT = __empty_peer_temp_storage;
    using _ItemType = sycl::nd_item<1>;
    using _SubGroupType = decltype(::std::declval<_ItemType>().get_sub_group());

    _SubGroupType __sgroup;
    ::std::uint32_t __sg_size;

    __peer_prefix_helper(sycl::nd_item<1> __self_item, _TempStorageT)
        : __sgroup(__self_item.get_sub_group()), __sg_size(__sgroup.get_local_range()[0])
    {
    }

    ::std::uint32_t
    __peer_contribution(_OffsetT& __new_offset_idx, _OffsetT __offset_prefix, bool __is_current_bucket)
    {
        ::std::uint32_t __sg_item_offset = __dpl_sycl::__exclusive_scan_over_group(
            __sgroup, static_cast<::std::uint32_t>(__is_current_bucket), __dpl_sycl::__plus<::std::uint32_t>());

        __new_offset_idx |= __is_current_bucket * (__offset_prefix + __sg_item_offset);
        // the last scanned value may not contain number of all copies, thus adding __is_current_bucket
        ::std::uint32_t __sg_total_offset =
            __dpl_sycl::__group_broadcast(__sgroup, __sg_item_offset + __is_current_bucket, __sg_size - 1);

        return __sg_total_offset;
    }
};

#if _ONEDPL_SYCL_SUB_GROUP_MASK_PRESENT
template <typename _OffsetT>
struct __peer_prefix_helper<_OffsetT, __peer_prefix_algo::subgroup_ballot>
{
    using _TempStorageT = __empty_peer_temp_storage;

    sycl::sub_group __sgroup;
    ::std::uint32_t __self_lidx;
    sycl::ext::oneapi::sub_group_mask __item_sg_mask;

    __peer_prefix_helper(sycl::nd_item<1> __self_item, _TempStorageT)
        : __sgroup(__self_item.get_sub_group()), __self_lidx(__self_item.get_local_linear_id()),
          __item_sg_mask(sycl::ext::oneapi::detail::Builder::createSubGroupMask<sycl::ext::oneapi::sub_group_mask>(
              ~(~0u << (__self_lidx)), __sgroup.get_local_linear_range()))
    {
    }

    ::std::uint32_t
    __peer_contribution(_OffsetT& __new_offset_idx, _OffsetT __offset_prefix, bool __is_current_bucket)
    {
        // set local id's bit to 1 if the bucket value matches the radix state
        auto __peer_mask = sycl::ext::oneapi::group_ballot(__sgroup, __is_current_bucket);
        ::std::uint32_t __peer_mask_bits{};
        __peer_mask.extract_bits(__peer_mask_bits);
        ::std::uint32_t __sg_total_offset = sycl::popcount(__peer_mask_bits);

        // get the local offset index from the bits set in the peer mask with index less than the work
        // items's ID
        __peer_mask &= __item_sg_mask;
        __peer_mask.extract_bits(__peer_mask_bits);
        __new_offset_idx |= __is_current_bucket * (__offset_prefix + sycl::popcount(__peer_mask_bits));

        return __sg_total_offset;
    }
};
#endif // _ONEDPL_SYCL_SUB_GROUP_MASK_PRESENT

//-----------------------------------------------------------------------
// radix sort: a function for reorder phase of one iteration
//-----------------------------------------------------------------------
template <typename _KernelName, ::std::uint32_t __radix_bits, bool __is_ascending, __peer_prefix_algo _PeerAlgo,
          typename _ExecutionPolicy, typename _InRange, typename _OutRange, typename _OffsetBuf
#if _ONEDPL_COMPILE_KERNEL
          , typename _Kernel
#endif
          >
sycl::event
__radix_sort_reorder_submit(_ExecutionPolicy&& __exec, ::std::size_t __segments, ::std::size_t __block_size,
                            ::std::size_t __sg_size, ::std::uint32_t __radix_offset, _InRange&& __input_rng,
                            _OutRange&& __output_rng, _OffsetBuf& __offset_buf, sycl::event __dependency_event
#if _ONEDPL_COMPILE_KERNEL
                            , _Kernel& __kernel
#endif
)
{
    // typedefs
    using _InputT = oneapi::dpl::__internal::__value_t<_InRange>;
    using _OffsetT = typename _OffsetBuf::value_type;
    using _PeerHelper = __peer_prefix_helper<_OffsetT, _PeerAlgo>;

    // iteration space info
    const ::std::size_t __inout_buf_size = __output_rng.size();
    constexpr ::std::uint32_t __radix_states = 1 << __radix_bits;
    const ::std::size_t __blocks_total = __ceiling_div(__inout_buf_size, __block_size);
    const ::std::size_t __blocks_per_segment = __ceiling_div(__blocks_total, __segments);

    auto __offset_rng =
        oneapi::dpl::__ranges::all_view<::std::uint32_t, __par_backend_hetero::access_mode::read>(__offset_buf);

    // submit to reorder values
    sycl::event __reorder_event = __exec.queue().submit([&](sycl::handler& __hdl) {
        __hdl.depends_on(__dependency_event);

        // access with offsets from each work group
        oneapi::dpl::__ranges::__require_access(__hdl, __offset_rng);

        // access with values to reorder and reordered values
        oneapi::dpl::__ranges::__require_access(__hdl, __input_rng, __output_rng);

        typename _PeerHelper::_TempStorageT __peer_temp(1, __hdl);

#if _ONEDPL_COMPILE_KERNEL && _ONEDPL_KERNEL_BUNDLE_PRESENT
        __hdl.use_kernel_bundle(__kernel.get_kernel_bundle());
#endif
        __hdl.parallel_for<_KernelName>(
#if _ONEDPL_COMPILE_KERNEL && !_ONEDPL_KERNEL_BUNDLE_PRESENT
            __kernel,
#endif
            sycl::nd_range<1>(__segments * __sg_size, __sg_size), [=](sycl::nd_item<1> __self_item) {
                // item info
                const ::std::size_t __self_lidx = __self_item.get_local_id(0);
                const ::std::size_t __wgroup_idx = __self_item.get_group(0);
                const ::std::size_t __start_idx = __blocks_per_segment * __block_size * __wgroup_idx + __self_lidx;

                _PeerHelper __peer_prefix_hlp(__self_item, __peer_temp);

                // 1. create a private array for storing offset values
                //    and add total offset and offset for compute unit for a certain radix state
                _OffsetT __offset_arr[__radix_states];
                const ::std::size_t __scan_size = __segments + 1;
                _OffsetT __scanned_bin = 0;
                __offset_arr[0] = __offset_rng[__wgroup_idx];
                for (::std::uint32_t __radix_state_idx = 1; __radix_state_idx < __radix_states; ++__radix_state_idx)
                {
                    const ::std::uint32_t __local_offset_idx = __wgroup_idx + (__segments + 1) * __radix_state_idx;

                    //scan bins (serial)
                    ::std::size_t __last_segment_bucket_idx = __radix_state_idx * __scan_size - 1;
                    __scanned_bin += __offset_rng[__last_segment_bucket_idx];

                    __offset_arr[__radix_state_idx] = __scanned_bin + __offset_rng[__local_offset_idx];
                }

                const ::std::size_t __outside_of_segment =
                    sycl::min(__start_idx + __block_size * __blocks_per_segment, __inout_buf_size);
                // find offsets for the same values within a segment and fill the resulting buffer
                for (::std::size_t __val_idx = __start_idx; __val_idx < __outside_of_segment; __val_idx += __sg_size)
                {
                    _InputT __in_val = __input_rng[__val_idx];
                    // get the bucket for the bit-ordered input value, applying the offset and mask for radix bits
                    ::std::uint32_t __bucket = __get_bucket<(1 << __radix_bits) - 1>(
                        __order_preserving_cast<__is_ascending>(__in_val), __radix_offset);

                    _OffsetT __new_offset_idx = 0;
                    for (::std::uint32_t __radix_state_idx = 0; __radix_state_idx < __radix_states; ++__radix_state_idx)
                    {
                        ::std::uint32_t __is_current_bucket = (__bucket == __radix_state_idx);
                        ::std::uint32_t __sg_total_offset = __peer_prefix_hlp.__peer_contribution(
                            __new_offset_idx, __offset_arr[__radix_state_idx], __is_current_bucket);
                        __offset_arr[__radix_state_idx] = __offset_arr[__radix_state_idx] + __sg_total_offset;
                    }
                    __output_rng[__new_offset_idx] = __in_val;
                }
            });
    });

    return __reorder_event;
}

//-----------------------------------------------------------------------
// radix sort: one iteration
//-----------------------------------------------------------------------

template <::std::uint32_t __radix_bits, bool __is_ascending, bool __even>
struct __parallel_radix_sort_iteration
{
    template <typename... _Name>
    using __count_phase = __radix_sort_count_kernel<__radix_bits, __is_ascending, __even, _Name...>;
    template <typename... _Name>
    using __local_scan_phase = __radix_sort_scan_kernel<__radix_bits, _Name...>;
    template <typename... _Name>
    using __reorder_peer_phase = __radix_sort_reorder_peer_kernel<__radix_bits, __is_ascending, __even, _Name...>;
    template <typename... _Name>
    using __reorder_phase = __radix_sort_reorder_kernel<__radix_bits, __is_ascending, __even, _Name...>;

    template <typename _ExecutionPolicy, typename _InRange, typename _OutRange, typename _TmpBuf>
    static sycl::event
    submit(_ExecutionPolicy&& __exec, ::std::size_t __segments, ::std::uint32_t __radix_iter,
           _InRange&& __in_rng, _OutRange&& __out_rng, _TmpBuf& __tmp_buf, sycl::event __dependency_event)
    {
        using _CustomName = typename __decay_t<_ExecutionPolicy>::kernel_name;
        using _RadixCountKernel = __internal::__kernel_name_generator<
            __count_phase, _CustomName, __decay_t<_InRange>, __decay_t<_TmpBuf>>;
        using _RadixLocalScanKernel = __internal::__kernel_name_generator<
            __local_scan_phase, _CustomName, __decay_t<_TmpBuf>>;
        using _RadixReorderPeerKernel = __internal::__kernel_name_generator<
            __reorder_peer_phase, _CustomName, __decay_t<_InRange>, __decay_t<_OutRange>>;
        using _RadixReorderKernel = __internal::__kernel_name_generator<
            __reorder_phase, _CustomName, __decay_t<_InRange>, __decay_t<_OutRange>>;

        ::std::size_t __max_sg_size = oneapi::dpl::__internal::__max_sub_group_size(__exec);
        ::std::size_t __scan_wg_size = oneapi::dpl::__internal::__max_work_group_size(__exec);
        ::std::size_t __block_size = __max_sg_size;
        ::std::size_t __reorder_sg_size = __max_sg_size;

        // correct __block_size, __scan_wg_size, __reorder_sg_size after introspection of the kernels
#if _ONEDPL_COMPILE_KERNEL
        auto __kernels = __internal::__kernel_compiler<_RadixCountKernel, _RadixLocalScanKernel,
                                                       _RadixReorderPeerKernel, _RadixReorderKernel>::__compile(__exec);
        auto __count_kernel = __kernels[0];
        auto __local_scan_kernel = __kernels[1];
        auto __reorder_peer_kernel = __kernels[2];
        auto __reorder_kernel = __kernels[3];
        ::std::size_t __count_sg_size = oneapi::dpl::__internal::__kernel_sub_group_size(__exec, __count_kernel);
        __reorder_sg_size = oneapi::dpl::__internal::__kernel_sub_group_size(__exec, __reorder_kernel);
        __scan_wg_size =
            sycl::min(__scan_wg_size, oneapi::dpl::__internal::__kernel_work_group_size(__exec, __local_scan_kernel));
        __block_size = sycl::max(__count_sg_size, __reorder_sg_size);
#endif
        const ::std::uint32_t __radix_states = 1 << __radix_bits;

        // correct __block_size according to local memory limit in count phase
        const auto __max_allocation_size = oneapi::dpl::__internal::__max_local_allocation_size(
            __exec, sizeof(typename __decay_t<_TmpBuf>::value_type), __block_size * __radix_states);
        __block_size = __ceiling_div(__max_allocation_size, __radix_states);

        // block size must be a power of 2 and not less than the number of states.
        // TODO: Check how to get rid of that restriction.
        __block_size = sycl::max(__dpl_bit_floor(__block_size), ::std::size_t(__radix_states));

        // Compute the radix position for the given iteration
        ::std::uint32_t __radix_offset = __radix_iter * __radix_bits;

        // 1. Count Phase
        sycl::event __count_event = __radix_sort_count_submit<_RadixCountKernel, __radix_bits, __is_ascending>(
            __exec, __segments, __block_size, __radix_offset, ::std::forward<_InRange>(__in_rng), __tmp_buf,
            __dependency_event
#if _ONEDPL_COMPILE_KERNEL
            , __count_kernel
#endif
        );

        // 2. Scan Phase
        sycl::event __scan_event = __radix_sort_scan_submit<_RadixLocalScanKernel, __radix_bits>(
            __exec, __scan_wg_size, __segments, __tmp_buf, __count_event
#if _ONEDPL_COMPILE_KERNEL
            , __local_scan_kernel
#endif
        );

        // 3. Reorder Phase
        sycl::event __reorder_event{};
        if (__reorder_sg_size == 8 || __reorder_sg_size == 16 || __reorder_sg_size == 32)
        {
#if _ONEDPL_SYCL_SUB_GROUP_MASK_PRESENT
            constexpr auto __peer_algorithm = __peer_prefix_algo::subgroup_ballot;
#elif _ONEDPL_LIBSYCL_VERSION >= 50700
            constexpr auto __peer_algorithm = __peer_prefix_algo::atomic_fetch_or;
#else
            constexpr auto __peer_algorithm = __peer_prefix_algo::scan_then_broadcast;
#endif // _ONEDPL_SYCL_SUB_GROUP_MASK_PRESENT

            __reorder_event = __radix_sort_reorder_submit<_RadixReorderPeerKernel, __radix_bits, __is_ascending,
                                                          __peer_algorithm>(
                __exec, __segments, __block_size, __reorder_sg_size, __radix_offset, ::std::forward<_InRange>(__in_rng),
                ::std::forward<_OutRange>(__out_rng), __tmp_buf, __scan_event
#if _ONEDPL_COMPILE_KERNEL
                , __reorder_peer_kernel
#endif
            );
        }
        else
        {
            __reorder_event = __radix_sort_reorder_submit<_RadixReorderKernel, __radix_bits, __is_ascending,
                                                          __peer_prefix_algo::scan_then_broadcast>(
                __exec, __segments, __block_size, __reorder_sg_size, __radix_offset, ::std::forward<_InRange>(__in_rng),
                ::std::forward<_OutRange>(__out_rng), __tmp_buf, __scan_event
#if _ONEDPL_COMPILE_KERNEL
                , __reorder_kernel
#endif
            );
        }

        return __reorder_event;
    }
}; // struct __parallel_radix_sort_iteration

//-----------------------------------------------------------------------
// radix sort: main function
//-----------------------------------------------------------------------

template <bool __is_ascending, typename _Range, typename _ExecutionPolicy>
auto
__parallel_radix_sort(_ExecutionPolicy&& __exec, _Range&& __in_rng)
{
    const ::std::size_t __n = __in_rng.size();
    assert(__n > 1);

    // types
    using _DecExecutionPolicy = __decay_t<_ExecutionPolicy>;
    using _T = oneapi::dpl::__internal::__value_t<_Range>;

    const ::std::size_t __wg_size = oneapi::dpl::__internal::__max_work_group_size(__exec);
    const ::std::size_t __segments = __ceiling_div(__n, __wg_size);

    // radix bits represent number of processed bits in each value during one iteration
    constexpr ::std::uint32_t __radix_bits = 4;
    constexpr ::std::uint32_t __radix_iters = __get_buckets_in_type<_T>(__radix_bits);
    const ::std::uint32_t __radix_states = 1 << __radix_bits;

    // additional __radix_states elements are used for getting local offsets from count values
    const ::std::size_t __tmp_buf_size = __segments * __radix_states + __radix_states;
    // memory for storing count and offset values
    auto __tmp_buf = sycl::buffer<::std::uint32_t, 1>(sycl::range<1>(__tmp_buf_size));

    // memory for storing values sorted for an iteration
    __internal::__buffer<_DecExecutionPolicy, _T> __out_buffer_holder{__exec, __n};
    auto __out_rng = oneapi::dpl::__ranges::all_view<_T, __par_backend_hetero::access_mode::read_write>(
        __out_buffer_holder.get_buffer());

    // iterations per each bucket
    assert("Number of iterations must be even" && __radix_iters % 2 == 0);
    // TODO: radix for bool can be made using 1 iteration (x2 speedup against current implementation)
    sycl::event __iteration_event{};
    for (::std::uint32_t __radix_iter = 0; __radix_iter < __radix_iters; ++__radix_iter)
    {
        // TODO: convert to ordered type once at the first iteration and convert back at the last one
        if (__radix_iter % 2 == 0)
            __iteration_event = __parallel_radix_sort_iteration<__radix_bits, __is_ascending, /*even=*/true>::submit(
                ::std::forward<_ExecutionPolicy>(__exec), __segments, __radix_iter,
                ::std::forward<_Range>(__in_rng), __out_rng, __tmp_buf, __iteration_event);
        else //swap __in_rng and __out_rng
            __iteration_event = __parallel_radix_sort_iteration<__radix_bits, __is_ascending, /*even=*/false>::submit(
                ::std::forward<_ExecutionPolicy>(__exec), __segments, __radix_iter,
                __out_rng, ::std::forward<_Range>(__in_rng), __tmp_buf, __iteration_event);

        // TODO: since reassign to __iteration_event does not work, we have to make explicit wait on the event
        explicit_wait_if<::std::is_pointer<decltype(__in_rng.begin())>::value>{}(__iteration_event);
    }

    return __future(__iteration_event, __tmp_buf, __out_buffer_holder.get_buffer());
}

} // namespace __par_backend_hetero
} // namespace dpl
} // namespace oneapi

#endif /* _ONEDPL_parallel_backend_sycl_radix_sort_H */
