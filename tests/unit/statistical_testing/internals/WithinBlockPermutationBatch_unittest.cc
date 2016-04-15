/*
 * Copyright (c) The Shogun Machine Learning Toolbox
 * Written (w) 2016 Soumyajit De
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are those
 * of the authors and should not be interpreted as representing official policies,
 * either expressed or implied, of the Shogun Development Team.
 */

#include <algorithm>
#include <shogun/base/some.h>
#include <shogun/lib/SGMatrix.h>
#include <shogun/lib/SGVector.h>
#include <shogun/features/Features.h>
#include <shogun/features/DenseFeatures.h>
#include <shogun/kernel/Kernel.h>
#include <shogun/kernel/GaussianKernel.h>
#include <shogun/mathematics/Math.h>
#include <shogun/mathematics/eigen3.h>
#include <shogun/statistical_testing/MMD.h>
#include <shogun/statistical_testing/internals/mmd/BiasedFull.h>
#include <shogun/statistical_testing/internals/mmd/UnbiasedFull.h>
#include <shogun/statistical_testing/internals/mmd/UnbiasedIncomplete.h>
#include <shogun/statistical_testing/internals/mmd/WithinBlockPermutationBatch.h>
#include <gtest/gtest.h>

using namespace shogun;
using namespace Eigen;

TEST(WithinBlockPermutationBatch, biased_full)
{
	const index_t dim=2;
	const index_t n=13;
	const index_t m=7;
	const index_t num_null_samples=5;

	using operation=std::function<float64_t(SGMatrix<float64_t>)>;

	SGMatrix<float64_t> data_p(dim, n);
	std::iota(data_p.matrix, data_p.matrix+dim*n, 1);
	std::for_each(data_p.matrix, data_p.matrix+dim*n, [&n](float64_t& val) { val/=n; });

	SGMatrix<float64_t> data_q(dim, m);
	std::iota(data_q.matrix, data_q.matrix+dim*m, n+1);
	std::for_each(data_q.matrix, data_q.matrix+dim*m, [&m](float64_t& val) { val/=2*m; });

	auto feats_p=new CDenseFeatures<float64_t>(data_p);
	auto feats_q=new CDenseFeatures<float64_t>(data_q);
	auto feats=feats_p->create_merged_copy(feats_q);
	SG_REF(feats);
	SG_UNREF(feats_p);
	SG_UNREF(feats_q);

	auto kernel=some<CGaussianKernel>();
	kernel->set_width(2.0);

	kernel->init(feats, feats);
	auto mat=kernel->get_kernel_matrix();

	// compute using within-block-permutation functor
	shogun::internal::mmd::WithinBlockPermutationBatch batch(n, m, num_null_samples, EStatisticType::BIASED_FULL);
	sg_rand->set_seed(12345);
	SGVector<float64_t> result_1=batch(mat);

	operation compute=shogun::internal::mmd::BiasedFull(n);

	// compute a row-column permuted temporary matrix first
	// then compute a biased-full statistic on this matrix
	Map<MatrixXd> map(mat.matrix, mat.num_rows, mat.num_cols);
	SGVector<float64_t> result_2(num_null_samples);
	sg_rand->set_seed(12345);
	for (auto i=0; i<num_null_samples; ++i)
	{
		PermutationMatrix<Dynamic, Dynamic> perm(mat.num_rows);
		perm.setIdentity();
		SGVector<int> perminds(perm.indices().data(), perm.indices().size(), false);
		CMath::permute(perminds);
		MatrixXd permuted = perm.transpose()*map*perm;
		SGMatrix<float64_t> permuted_km(permuted.data(), permuted.rows(), permuted.cols(), false);
		result_2[i]=compute(permuted_km);
	}

	// shuffle the features first, recompute the kernel matrix using
	// shuffled samples, then compute a biased-full statistic on this matrix
	SGVector<index_t> inds(mat.num_rows);
	SGVector<float64_t> result_3(num_null_samples);
	sg_rand->set_seed(12345);
	for (auto i=0; i<num_null_samples; ++i)
	{
		std::iota(inds.vector, inds.vector+inds.vlen, 0);
		CMath::permute(inds);
		feats->add_subset(inds);
		kernel->init(feats, feats);
		mat=kernel->get_kernel_matrix();
		result_3[i]=compute(mat);
		feats->remove_subset();
	}

	for (auto i=0; i<num_null_samples; ++i)
	{
		EXPECT_NEAR(result_1[i], result_2[i], 1E-14);
		EXPECT_NEAR(result_1[i], result_3[i], 1E-14);
	}

	SG_UNREF(feats);
}

TEST(WithinBlockPermutationBatch, unbiased_full)
{
	const index_t dim=2;
	const index_t n=13;
	const index_t m=7;
	const index_t num_null_samples=5;

	using operation=std::function<float64_t(SGMatrix<float64_t>)>;

	SGMatrix<float64_t> data_p(dim, n);
	std::iota(data_p.matrix, data_p.matrix+dim*n, 1);
	std::for_each(data_p.matrix, data_p.matrix+dim*n, [&n](float64_t& val) { val/=n; });

	SGMatrix<float64_t> data_q(dim, m);
	std::iota(data_q.matrix, data_q.matrix+dim*m, n+1);
	std::for_each(data_q.matrix, data_q.matrix+dim*m, [&m](float64_t& val) { val/=2*m; });

	auto feats_p=new CDenseFeatures<float64_t>(data_p);
	auto feats_q=new CDenseFeatures<float64_t>(data_q);
	auto feats=feats_p->create_merged_copy(feats_q);
	SG_REF(feats);
	SG_UNREF(feats_p);
	SG_UNREF(feats_q);

	auto kernel=some<CGaussianKernel>();
	kernel->set_width(2.0);

	kernel->init(feats, feats);
	auto mat=kernel->get_kernel_matrix();

	// compute using within-block-permutation functor
    shogun::internal::mmd::WithinBlockPermutationBatch batch(n, m, num_null_samples, EStatisticType::UNBIASED_FULL);
	sg_rand->set_seed(12345);
	SGVector<float64_t> result_1=batch(mat);

	operation compute=shogun::internal::mmd::UnbiasedFull(n);

	// compute a row-column permuted temporary matrix first
	// then compute unbiased-full statistic on this matrix
	Map<MatrixXd> map(mat.matrix, mat.num_rows, mat.num_cols);
	SGVector<float64_t> result_2(num_null_samples);
	sg_rand->set_seed(12345);
	for (auto i=0; i<num_null_samples; ++i)
	{
		PermutationMatrix<Dynamic, Dynamic> perm(mat.num_rows);
		perm.setIdentity();
		SGVector<int> perminds(perm.indices().data(), perm.indices().size(), false);
		CMath::permute(perminds);
		MatrixXd permuted = perm.transpose()*map*perm;
		SGMatrix<float64_t> permuted_km(permuted.data(), permuted.rows(), permuted.cols(), false);
		result_2[i]=compute(permuted_km);
	}

	// shuffle the features first, recompute the kernel matrix using
	// shuffled samples, then compute unbiased-full statistic on this matrix
	SGVector<index_t> inds(mat.num_rows);
	SGVector<float64_t> result_3(num_null_samples);
	sg_rand->set_seed(12345);
	for (auto i=0; i<num_null_samples; ++i)
	{
		std::iota(inds.vector, inds.vector+inds.vlen, 0);
		CMath::permute(inds);
		feats->add_subset(inds);
		kernel->init(feats, feats);
		mat=kernel->get_kernel_matrix();
		result_3[i]=compute(mat);
		feats->remove_subset();
	}

	for (auto i=0; i<num_null_samples; ++i)
	{
		EXPECT_NEAR(result_1[i], result_2[i], 1E-14);
		EXPECT_NEAR(result_1[i], result_3[i], 1E-14);
	}

	SG_UNREF(feats);
}

TEST(WithinBlockPermutationBatch, unbiased_incomplete)
{
	const index_t dim=2;
	const index_t n=10;
	const index_t num_null_samples=5;

	using operation=std::function<float64_t(SGMatrix<float64_t>)>;

	SGMatrix<float64_t> data_p(dim, n);
	std::iota(data_p.matrix, data_p.matrix+dim*n, 1);
	std::for_each(data_p.matrix, data_p.matrix+dim*n, [&n](float64_t& val) { val/=n; });

	SGMatrix<float64_t> data_q(dim, n);
	std::iota(data_q.matrix, data_q.matrix+dim*n, n+1);
	std::for_each(data_q.matrix, data_q.matrix+dim*n, [&n](float64_t& val) { val/=2*n; });

	auto feats_p=new CDenseFeatures<float64_t>(data_p);
	auto feats_q=new CDenseFeatures<float64_t>(data_q);
	auto feats=feats_p->create_merged_copy(feats_q);
	SG_REF(feats);
	SG_UNREF(feats_p);
	SG_UNREF(feats_q);

	auto kernel=some<CGaussianKernel>();
	kernel->set_width(2.0);

	kernel->init(feats, feats);
	auto mat=kernel->get_kernel_matrix();

	// compute using within-block-permutation functor
	shogun::internal::mmd::WithinBlockPermutationBatch batch(n, n, num_null_samples, EStatisticType::UNBIASED_INCOMPLETE);
	sg_rand->set_seed(12345);
	SGVector<float64_t> result_1=batch(mat);

	operation compute=shogun::internal::mmd::UnbiasedIncomplete(n);

	// compute a row-column permuted temporary matrix first
	// then compute unbiased-incomplete statistic on this matrix
	Map<MatrixXd> map(mat.matrix, mat.num_rows, mat.num_cols);
	SGVector<float64_t> result_2(num_null_samples);
	sg_rand->set_seed(12345);
	for (auto i=0; i<num_null_samples; ++i)
	{
		PermutationMatrix<Dynamic, Dynamic> perm(mat.num_rows);
		perm.setIdentity();
		SGVector<int> perminds(perm.indices().data(), perm.indices().size(), false);
		CMath::permute(perminds);
		MatrixXd permuted = perm.transpose()*map*perm;
		SGMatrix<float64_t> permuted_km(permuted.data(), permuted.rows(), permuted.cols(), false);
		result_2[i]=compute(permuted_km);
	}

	// shuffle the features first, recompute the kernel matrix using
	// shuffled samples, then compute uniased-incomplete statistic on this matrix
	SGVector<index_t> inds(mat.num_rows);
	SGVector<float64_t> result_3(num_null_samples);
	sg_rand->set_seed(12345);
	for (auto i=0; i<num_null_samples; ++i)
	{
		std::iota(inds.vector, inds.vector+inds.vlen, 0);
		CMath::permute(inds);
		feats->add_subset(inds);
		kernel->init(feats, feats);
		mat=kernel->get_kernel_matrix();
		result_3[i]=compute(mat);
		feats->remove_subset();
	}

	for (auto i=0; i<num_null_samples; ++i)
	{
		EXPECT_NEAR(result_1[i], result_2[i], 1E-14);
		EXPECT_NEAR(result_1[i], result_3[i], 1E-14);
	}

	SG_UNREF(feats);
}