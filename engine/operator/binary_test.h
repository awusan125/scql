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

#include "arrow/type.h"
#include "gtest/gtest.h"

#include "engine/core/tensor_from_json.h"
#include "engine/operator/all_ops_register.h"
#include "engine/operator/binary_base.h"
#include "engine/operator/test_util.h"

namespace scql::engine::op {

struct BinaryTestCase {
  std::string op_type;

  std::vector<test::NamedTensor> left_inputs;
  pb::TensorStatus left_input_status;

  std::vector<test::NamedTensor> right_inputs;
  pb::TensorStatus right_input_status;

  std::vector<test::NamedTensor> outputs;
  pb::TensorStatus output_status;
};

class BinaryTest : public testing::TestWithParam<
                       std::tuple<spu::ProtocolKind, BinaryTestCase>> {
 protected:
  void SetUp() override { RegisterAllOps(); }

 public:
  static pb::ExecNode MakeExecNode(const BinaryTestCase& tc);

  static void FeedInputs(const std::vector<ExecContext*>& ctxs,
                         const BinaryTestCase& tc);

  static std::unique_ptr<Operator> CreateOp(const std::string& op_type);
};

class BinaryComputeInSecretTest : public BinaryTest {};
class BinaryComputeInPlainTest : public BinaryTest {};

TEST_P(BinaryComputeInSecretTest, Works) {
  // Given
  auto parm = GetParam();
  auto tc = std::get<1>(parm);
  auto node = MakeExecNode(tc);
  std::vector<Session> sessions = test::Make2PCSession(std::get<0>(parm));

  ExecContext alice_ctx(node, &sessions[0]);
  ExecContext bob_ctx(node, &sessions[1]);

  FeedInputs(std::vector<ExecContext*>{&alice_ctx, &bob_ctx}, tc);

  // When
  auto alice_op = CreateOp(node.op_type());
  auto bob_op = CreateOp(node.op_type());
  ASSERT_TRUE(alice_op != nullptr)
      << "failed to create operator for op_type = " << node.op_type();
  ASSERT_TRUE(bob_op != nullptr);

  test::OpAsyncRunner alice(alice_op.get());
  test::OpAsyncRunner bob(bob_op.get());

  alice.Start(&alice_ctx);
  bob.Start(&bob_ctx);

  // Then
  EXPECT_NO_THROW({ alice.Wait(); });
  EXPECT_NO_THROW({ bob.Wait(); });

  for (const auto& named_tensor : tc.outputs) {
    TensorPtr t = nullptr;
    EXPECT_NO_THROW({
      t = test::RevealSecret({&alice_ctx, &bob_ctx}, named_tensor.name);
    });
    ASSERT_TRUE(t != nullptr);
    EXPECT_TRUE(t->ToArrowChunkedArray()->ApproxEquals(
        *named_tensor.tensor->ToArrowChunkedArray(),
        arrow::EqualOptions::Defaults().atol(0.05)))
        << "expect type = "
        << named_tensor.tensor->ToArrowChunkedArray()->type()->ToString()
        << ", got type = " << t->ToArrowChunkedArray()->type()->ToString()
        << "\nexpect result = "
        << named_tensor.tensor->ToArrowChunkedArray()->ToString()
        << "\nbut actual got result = " << t->ToArrowChunkedArray()->ToString();
  }
}

TEST_P(BinaryComputeInPlainTest, Works) {
  auto parm = GetParam();
  auto tc = std::get<1>(parm);
  auto node = MakeExecNode(tc);
  std::vector<Session> sessions = test::Make2PCSession(std::get<0>(parm));

  ExecContext alice_ctx(node, &sessions[0]);
  ExecContext bob_ctx(node, &sessions[1]);

  FeedInputs(std::vector<ExecContext*>{&alice_ctx, &bob_ctx}, tc);

  // When
  auto alice_op = CreateOp(node.op_type());
  ASSERT_TRUE(alice_op != nullptr)
      << "failed to create operator for op_type = " << node.op_type();

  // Then
  EXPECT_NO_THROW({ alice_op->Run(&alice_ctx); });

  auto tensor_table = alice_ctx.GetTensorTable();
  for (const auto& named_tensor : tc.outputs) {
    auto t = tensor_table->GetTensor(named_tensor.name);
    EXPECT_TRUE(t != nullptr) << named_tensor.name << " not found";

    EXPECT_TRUE(t->ToArrowChunkedArray()->ApproxEquals(
        *named_tensor.tensor->ToArrowChunkedArray()))
        << "expect type = "
        << named_tensor.tensor->ToArrowChunkedArray()->type()->ToString()
        << ", got type = " << t->ToArrowChunkedArray()->type()->ToString()
        << "\nexpect result = "
        << named_tensor.tensor->ToArrowChunkedArray()->ToString()
        << "\nbut actual got result = " << t->ToArrowChunkedArray()->ToString();
  }
}

// ==========================
// BinaryTest impl
// ==========================

pb::ExecNode BinaryTest::MakeExecNode(const BinaryTestCase& tc) {
  test::ExecNodeBuilder builder(tc.op_type);

  builder.SetNodeName(tc.op_type + "-test");

  auto build_io = [](const std::vector<test::NamedTensor>& ts,
                     pb::TensorStatus visibility) -> std::vector<pb::Tensor> {
    std::vector<pb::Tensor> tensors;
    for (const auto& named_tensor : ts) {
      auto t = test::MakeTensorReference(
          named_tensor.name, named_tensor.tensor->Type(), visibility);
      tensors.push_back(std::move(t));
    }
    return tensors;
  };

  builder.AddInput(BinaryBase::kInLeft,
                   build_io(tc.left_inputs, tc.left_input_status));
  builder.AddInput(BinaryBase::kInRight,
                   build_io(tc.right_inputs, tc.right_input_status));
  builder.AddOutput(BinaryBase::kOut, build_io(tc.outputs, tc.output_status));

  return builder.Build();
}

void BinaryTest::FeedInputs(const std::vector<ExecContext*>& ctxs,
                            const BinaryTestCase& tc) {
  auto infeed = [](const std::vector<ExecContext*>& ctxs,
                   pb::TensorStatus input_status,
                   const std::vector<test::NamedTensor>& inputs) {
    if (input_status == pb::TENSORSTATUS_PRIVATE) {
      // feed private inputs into ctxs[0]
      test::FeedInputsAsPrivate(ctxs[0], inputs);
    } else if (input_status == pb::TENSORSTATUS_SECRET) {
      test::FeedInputsAsSecret(ctxs, inputs);
    } else {
      test::FeedInputsAsPublic(ctxs, inputs);
    }
  };

  infeed(ctxs, tc.left_input_status, tc.left_inputs);
  infeed(ctxs, tc.right_input_status, tc.right_inputs);
}

std::unique_ptr<Operator> BinaryTest::CreateOp(const std::string& op_type) {
  return GetOpRegistry()->GetOperator(op_type);
}

}  // namespace scql::engine::op