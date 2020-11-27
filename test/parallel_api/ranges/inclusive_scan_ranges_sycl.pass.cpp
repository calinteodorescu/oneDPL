// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Copyright (C) 2020 Intel Corporation
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

#include "support/pstl_test_config.h"

#include _PSTL_TEST_HEADER(execution)
#include _PSTL_TEST_HEADER(numeric)
#if _ONEDPL_USE_RANGES
#include _PSTL_TEST_HEADER(ranges)
#endif

#include "support/utils.h"

#include <iostream>

int32_t
main()
{
#if _ONEDPL_USE_RANGES
    constexpr int max_n = 10;
    int data[max_n] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    int data1[max_n], data2[max_n], data3[max_n];

    {
        sycl::buffer<int> A(data, sycl::range<1>(max_n));
        sycl::buffer<int> B1(data1, sycl::range<1>(max_n));
        sycl::buffer<int> B2(data2, sycl::range<1>(max_n));
        sycl::buffer<int> B3(data3, sycl::range<1>(max_n));

        using namespace TestUtils;
        using namespace oneapi::dpl::experimental;

        auto view = ranges::all_view<int, sycl::access::mode::read>(A);
        auto view_res1 = ranges::all_view<int, sycl::access::mode::write>(B1);
        auto view_res2 = ranges::all_view<int, sycl::access::mode::write>(B2);
        auto view_res3 = ranges::all_view<int, sycl::access::mode::write>(B3);

        auto exec = TestUtils::default_dpcpp_policy;
        using Policy = decltype(TestUtils::default_dpcpp_policy);

        ranges::inclusive_scan(exec, view, view_res1);
        ranges::inclusive_scan(make_new_policy<new_kernel_name<Policy, 0>>(exec), view, view_res2, ::std::plus<int>());
        ranges::inclusive_scan(make_new_policy<new_kernel_name<Policy, 1>>(exec), view, view_res3, ::std::plus<int>(), 100);
    }

    //check result
    int expected1[max_n], expected2[max_n], expected3[max_n];
    ::std::inclusive_scan(oneapi::dpl::execution::seq, data, data + max_n, expected1);
    ::std::inclusive_scan(oneapi::dpl::execution::seq, data, data + max_n, expected2, ::std::plus<int>());
    ::std::inclusive_scan(oneapi::dpl::execution::seq, data, data + max_n, expected3, ::std::plus<int>(), 100);

    EXPECT_EQ_N(expected1, data1, max_n, "wrong effect from inclusive_scan with sycl ranges");
    EXPECT_EQ_N(expected2, data2, max_n, "wrong effect from inclusive_scan with binary operation, sycl ranges");
    EXPECT_EQ_N(expected3, data3, max_n, "wrong effect from inclusive_scan with binary operation and init, sycl ranges");

#endif //_ONEDPL_USE_RANGES
    ::std::cout << TestUtils::done() << ::std::endl;
    return 0;
}
