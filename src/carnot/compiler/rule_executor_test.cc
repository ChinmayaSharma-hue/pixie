#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

#include <pypa/parser/parser.hh>

#include "src/carnot/compiler/rule_executor.h"
#include "src/carnot/compiler/rule_mock.h"
#include "src/carnot/compiler/test_utils.h"
#include "src/carnot/udf_exporter/udf_exporter.h"

namespace pl {
namespace carnot {
namespace compiler {

using testing::_;

class RuleExecutorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    info_ = udfexporter::ExportUDFInfo().ConsumeValueOrDie();

    auto rel_map = std::make_unique<RelationMap>();
    auto cpu_relation = table_store::schema::Relation(
        std::vector<types::DataType>({types::DataType::INT64, types::DataType::FLOAT64,
                                      types::DataType::FLOAT64, types::DataType::FLOAT64}),
        std::vector<std::string>({"count", "cpu0", "cpu1", "cpu2"}));
    rel_map->emplace("cpu", cpu_relation);

    compiler_state_ = std::make_unique<CompilerState>(std::move(rel_map), info_.get(), time_now);

    ast = MakeTestAstPtr();
    graph = std::make_shared<IR>();
    mem_src = graph->MakeNode<MemorySourceIR>().ValueOrDie();
    PL_CHECK_OK(mem_src->SetRelation(cpu_relation));
    SetupGraph();
  }
  void SetupGraph() {
    map = graph->MakeNode<MapIR>().ValueOrDie();
    int_constant = graph->MakeNode<IntIR>().ValueOrDie();
    PL_CHECK_OK(int_constant->Init(10, ast));
    int_constant2 = graph->MakeNode<IntIR>().ValueOrDie();
    PL_CHECK_OK(int_constant2->Init(12, ast));
    col = graph->MakeNode<ColumnIR>().ValueOrDie();
    PL_CHECK_OK(col->Init("count", ast));
    func = graph->MakeNode<FuncIR>().ValueOrDie();
    func2 = graph->MakeNode<FuncIR>().ValueOrDie();
    lambda = graph->MakeNode<LambdaIR>().ValueOrDie();
    PL_CHECK_OK(func->Init({FuncIR::Opcode::add, "+", "add"}, ASTWalker::kRunTimeFuncPrefix,
                           std::vector<ExpressionIR*>({int_constant, col}),
                           false /* compile_time */, ast));
    PL_CHECK_OK(func2->Init({FuncIR::Opcode::add, "+", "add"}, ASTWalker::kRunTimeFuncPrefix,
                            std::vector<ExpressionIR*>({int_constant2, func}),
                            false /* compile_time */, ast));
    PL_CHECK_OK(lambda->Init({"count"}, {{"func", func2}}, ast));
    ArgMap amap({{"fn", lambda}});
    PL_CHECK_OK(map->Init(mem_src, amap, ast));
  }
  std::unique_ptr<CompilerState> compiler_state_;
  std::unique_ptr<RegistryInfo> info_;
  int64_t time_now = 1552607213931245000;
  pypa::AstPtr ast;
  std::shared_ptr<IR> graph;
  MemorySourceIR* mem_src;
  FuncIR* func;
  FuncIR* func2;
  MapIR* map;
  IntIR* int_constant;
  IntIR* int_constant2;
  LambdaIR* lambda;
  ColumnIR* col;
};

class TestExecutor : public RuleExecutor {
 public:
  static StatusOr<std::unique_ptr<TestExecutor>> Create() {
    std::unique_ptr<TestExecutor> executor(new TestExecutor());
    return std::move(executor);
  }
};

TEST(StrategyTest, fail_on_max) {
  int64_t num_iterations = 10;
  auto s = std::make_unique<FailOnMax>(num_iterations);
  Strategy* s_down_cast = s.get();
  EXPECT_EQ(s_down_cast->max_iterations(), num_iterations);
  Status status = s_down_cast->MaxIterationsHandler();
  EXPECT_NOT_OK(status);
  EXPECT_EQ(status.msg(), "Max iterations reached.");
}

TEST(StrategyTest, try_until_max) {
  int64_t num_iterations = 10;
  auto s = std::make_unique<TryUntilMax>(num_iterations);
  Strategy* s_down_cast = s.get();
  EXPECT_EQ(s_down_cast->max_iterations(), num_iterations);
  Status status = s_down_cast->MaxIterationsHandler();
  EXPECT_OK(status);
}

TEST(StrategyTest, do_once) {
  auto s = std::make_unique<DoOnce>();
  Strategy* s_down_cast = s.get();
  EXPECT_EQ(s_down_cast->max_iterations(), 1);
  Status status = s_down_cast->MaxIterationsHandler();
  EXPECT_OK(status);
}

using ::testing::Return;

// Tests that rule execution works as expected in simple 1 batch case.
TEST_F(RuleExecutorTest, rule_executor_test) {
  std::unique_ptr<TestExecutor> executor = std::move(TestExecutor::Create().ValueOrDie());
  RuleBatch* rule_batch = executor->CreateRuleBatch<FailOnMax>("resolve", 10);
  MockRule* rule1 = rule_batch->AddRule<MockRule>(compiler_state_.get());
  EXPECT_CALL(*rule1, Execute(_)).Times(2).WillOnce(Return(true)).WillRepeatedly(Return(false));
  ASSERT_OK(executor->Execute(graph.get()));
}

// // Tests to see that rules in different batches can run.
TEST_F(RuleExecutorTest, multiple_rule_batches) {
  std::unique_ptr<TestExecutor> executor = std::move(TestExecutor::Create().ValueOrDie());
  RuleBatch* rule_batch1 = executor->CreateRuleBatch<FailOnMax>("resolve", 10);
  MockRule* rule1_1 = rule_batch1->AddRule<MockRule>(compiler_state_.get());
  EXPECT_CALL(*rule1_1, Execute(_)).Times(2).WillRepeatedly(Return(false));
  MockRule* rule1_2 = rule_batch1->AddRule<MockRule>(compiler_state_.get());
  EXPECT_CALL(*rule1_2, Execute(_)).Times(2).WillOnce(Return(true)).WillRepeatedly(Return(false));

  RuleBatch* rule_batch2 = executor->CreateRuleBatch<FailOnMax>("resolve", 10);
  MockRule* rule2_1 = rule_batch2->AddRule<MockRule>(compiler_state_.get());
  EXPECT_CALL(*rule2_1, Execute(_)).Times(2).WillOnce(Return(true)).WillRepeatedly(Return(false));
  EXPECT_OK(executor->Execute(graph.get()));
}

// Tests to see that within a rule batch, rules will run if their sibling rule changes the graph.
TEST_F(RuleExecutorTest, rules_in_batch_correspond) {
  std::unique_ptr<TestExecutor> executor = std::move(TestExecutor::Create().ValueOrDie());
  RuleBatch* rule_batch1 = executor->CreateRuleBatch<FailOnMax>("resolve", 10);
  MockRule* rule1_1 = rule_batch1->AddRule<MockRule>(compiler_state_.get());
  EXPECT_CALL(*rule1_1, Execute(_))
      .Times(3)
      .WillOnce(Return(false))
      .WillOnce(Return(true))
      .WillOnce(Return(false));

  MockRule* rule1_2 = rule_batch1->AddRule<MockRule>(compiler_state_.get());
  EXPECT_CALL(*rule1_2, Execute(_)).Times(3).WillOnce(Return(true)).WillRepeatedly(Return(false));

  EXPECT_OK(executor->Execute(graph.get()));
}

// Test to see that if the strategy exits, then following batches don't run.
TEST_F(RuleExecutorTest, exit_early) {
  std::unique_ptr<TestExecutor> executor = std::move(TestExecutor::Create().ValueOrDie());
  RuleBatch* rule_batch1 = executor->CreateRuleBatch<FailOnMax>("resolve", 10);
  MockRule* rule1_1 = rule_batch1->AddRule<MockRule>(compiler_state_.get());
  EXPECT_CALL(*rule1_1, Execute(_)).Times(10).WillRepeatedly(Return(true));

  RuleBatch* rule_batch2 = executor->CreateRuleBatch<FailOnMax>("resolve", 10);
  MockRule* rule2_1 = rule_batch2->AddRule<MockRule>(compiler_state_.get());
  EXPECT_CALL(*rule2_1, Execute(_)).Times(0);

  EXPECT_NOT_OK(executor->Execute(graph.get()));
}

}  // namespace compiler
}  // namespace carnot
}  // namespace pl
