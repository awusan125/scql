// Copyright 2023 Ant Group Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "engine/operator/arithmetic.h"

#include "arrow/compute/api_scalar.h"
#include "arrow/compute/cast.h"
#include "arrow/datum.h"
#include "arrow/result.h"
#include "libspu/kernel/hal/type_cast.h"
#include "libspu/kernel/hlo/basic_binary.h"

#include "engine/util/spu_io.h"
#include "engine/util/tensor_util.h"

namespace scql::engine::op {

void ArithmeticBase::ValidateIoDataTypes(ExecContext* ctx) {
  // TODO(shunde.csd): check input and output data types for arithmetic ops
}

// ===========================
//   Add impl
// ===========================

const std::string Add::kOpType("Add");

const std::string& Add::Type() const { return kOpType; }

spu::Value Add::ComputeOnSpu(spu::HalContext* hctx, const spu::Value& lhs,
                             const spu::Value& rhs) {
  return spu::kernel::hlo::Add(hctx, lhs, rhs);
}

TensorPtr Add::ComputeInPlain(const Tensor& lhs, const Tensor& rhs) {
  arrow::Result<arrow::Datum> result = arrow::compute::CallFunction(
      "add", {lhs.ToArrowChunkedArray(), rhs.ToArrowChunkedArray()});

  YACL_ENFORCE(result.ok(),
               "caught error while invoking arrow add function: {}",
               result.status().ToString());
  return std::make_shared<Tensor>(result.ValueOrDie().chunked_array());
}

// ===========================
//   Minus impl
// ===========================

const std::string Minus::kOpType("Minus");

const std::string& Minus::Type() const { return kOpType; }

spu::Value Minus::ComputeOnSpu(spu::HalContext* hctx, const spu::Value& lhs,
                               const spu::Value& rhs) {
  return spu::kernel::hlo::Sub(hctx, lhs, rhs);
}

TensorPtr Minus::ComputeInPlain(const Tensor& lhs, const Tensor& rhs) {
  arrow::Result<arrow::Datum> result = arrow::compute::CallFunction(
      "subtract", {lhs.ToArrowChunkedArray(), rhs.ToArrowChunkedArray()});

  YACL_ENFORCE(result.ok(),
               "caught error while invoking arrow subtract function: {}",
               result.status().ToString());
  return std::make_shared<Tensor>(result.ValueOrDie().chunked_array());
}

// ===========================
//   Mul impl
// ===========================

const std::string Mul::kOpType("Mul");

const std::string& Mul::Type() const { return kOpType; }

spu::Value Mul::ComputeOnSpu(spu::HalContext* hctx, const spu::Value& lhs,
                             const spu::Value& rhs) {
  return spu::kernel::hlo::Mul(hctx, lhs, rhs);
}

TensorPtr Mul::ComputeInPlain(const Tensor& lhs, const Tensor& rhs) {
  arrow::Result<arrow::Datum> result = arrow::compute::CallFunction(
      "multiply", {lhs.ToArrowChunkedArray(), rhs.ToArrowChunkedArray()});

  YACL_ENFORCE(result.ok(),
               "caught error while invoking arrow multiply function: {}",
               result.status().ToString());
  return std::make_shared<Tensor>(result.ValueOrDie().chunked_array());
}

// ===========================
//   Div impl
// ===========================

const std::string Div::kOpType("Div");

const std::string& Div::Type() const { return kOpType; }

spu::Value Div::ComputeOnSpu(spu::HalContext* hctx, const spu::Value& lhs,
                             const spu::Value& rhs) {
  if (lhs.isInt() && rhs.isInt()) {
    // if lhs and rhs are both integers, the result will be integer.
    // so we convert lhs to float here.
    const auto lhs_f = spu::kernel::hal::dtype_cast(hctx, lhs, spu::DT_FXP);
    return spu::kernel::hlo::Div(hctx, lhs_f, rhs);
  }
  return spu::kernel::hlo::Div(hctx, lhs, rhs);
}

TensorPtr Div::ComputeInPlain(const Tensor& lhs, const Tensor& rhs) {
  // cast lhs to float64 if both lhs and rhs are integer
  auto left = lhs.ToArrowChunkedArray();
  auto right = rhs.ToArrowChunkedArray();
  if (arrow::is_integer(left->type()->id()) &&
      arrow::is_integer(right->type()->id())) {
    auto cast_options =
        arrow::compute::CastOptions::Safe(arrow::TypeHolder(arrow::float64()));
    auto result = arrow::compute::Cast(left, cast_options);
    YACL_ENFORCE(result.ok(), "Fail to cast lhs type to float64: {}",
                 result.status().ToString());
    left = result.ValueOrDie().chunked_array();
  }
  arrow::Result<arrow::Datum> result =
      arrow::compute::CallFunction("divide", {left, right});
  YACL_ENFORCE(result.ok(),
               "caught error while invoking arrow divide function: {}",
               result.status().ToString());
  return std::make_shared<Tensor>(result.ValueOrDie().chunked_array());
}

// ===========================
//   IntDiv impl
// ===========================

const std::string IntDiv::kOpType("IntDiv");

const std::string& IntDiv::Type() const { return kOpType; }

spu::Value IntDiv::ComputeOnSpu(spu::HalContext* hctx, const spu::Value& lhs,
                                const spu::Value& rhs) {
  // NOTE(shunde.csd): if lhs and rhs are both integers,
  // `spu::kernel::hlo::Div` will behave like `IntDiv`
  return spu::kernel::hlo::Div(hctx, lhs, rhs);
}

TensorPtr IntDiv::ComputeInPlain(const Tensor& lhs, const Tensor& rhs) {
  // NOTE(shunde.csd): if lhs and rhs are both integers,
  // arrow `divide` function will behave like `IntDiv`
  arrow::Result<arrow::Datum> result = arrow::compute::CallFunction(
      "divide", {lhs.ToArrowChunkedArray(), rhs.ToArrowChunkedArray()});

  YACL_ENFORCE(result.ok(),
               "caught error while invoking arrow divide function: {}",
               result.status().ToString());
  return std::make_shared<Tensor>(result.ValueOrDie().chunked_array());
}

// ===========================
//   Mod impl
// ===========================

const std::string Mod::kOpType("Mod");

const std::string& Mod::Type() const { return kOpType; }

spu::Value Mod::ComputeOnSpu(spu::HalContext* hctx, const spu::Value& lhs,
                             const spu::Value& rhs) {
  YACL_THROW("unimplemented");
}

TensorPtr Mod::ComputeInPlain(const Tensor& lhs, const Tensor& rhs) {
  YACL_THROW("unimplemented");
  return nullptr;
}

}  // namespace scql::engine::op