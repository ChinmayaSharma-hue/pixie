/*
 * Copyright 2018- The Pixie Authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <queue>

#include "src/carnot/planner/ir/column_ir.h"
#include "src/carnot/planner/ir/ir.h"

namespace px {
namespace carnot {
namespace planner {

Status ColumnIR::Init(const std::string& col_name, int64_t parent_idx) {
  SetColumnName(col_name);
  SetContainingOperatorParentIdx(parent_idx);
  return Status::OK();
}

StatusOr<int64_t> ColumnIR::GetColumnIndex() const {
  PL_ASSIGN_OR_RETURN(auto op, ReferencedOperator());
  if (!op->relation().HasColumn(col_name())) {
    return DExitOrIRNodeError("Column '$0' does not exist in relation $1", col_name(),
                              op->relation().DebugString());
  }
  return op->relation().GetColumnIndex(col_name());
}

Status ColumnIR::ToProto(planpb::Column* column_pb) const {
  PL_ASSIGN_OR_RETURN(int64_t ref_op_id, ReferenceID());
  column_pb->set_node(ref_op_id);
  PL_ASSIGN_OR_RETURN(auto index, GetColumnIndex());
  column_pb->set_index(index);
  return Status::OK();
}

Status ColumnIR::ToProto(planpb::ScalarExpression* expr) const {
  auto column_pb = expr->mutable_column();
  return ToProto(column_pb);
}

std::string ColumnIR::DebugString() const {
  return absl::Substitute("$0(id=$1, name=$2)", type_string(), id(), col_name());
}

void ColumnIR::SetContainingOperatorParentIdx(int64_t container_op_parent_idx) {
  DCHECK_GE(container_op_parent_idx, 0);
  container_op_parent_idx_ = container_op_parent_idx;
  container_op_parent_idx_set_ = true;
}

StatusOr<std::vector<OperatorIR*>> ColumnIR::ContainingOperators() const {
  std::vector<OperatorIR*> parents;
  std::queue<int64_t> cur_ids;
  cur_ids.push(id());

  while (cur_ids.size()) {
    auto cur_id = cur_ids.front();
    cur_ids.pop();
    IRNode* cur_node = graph()->Get(cur_id);
    if (cur_node->IsOperator()) {
      parents.push_back(static_cast<OperatorIR*>(cur_node));
      continue;
    }
    std::vector<int64_t> parents = graph()->dag().ParentsOf(cur_id);
    for (auto parent_id : parents) {
      cur_ids.push(parent_id);
    }
  }
  return parents;
}

StatusOr<OperatorIR*> ColumnIR::ReferencedOperator() const {
  DCHECK(container_op_parent_idx_set_);
  PL_ASSIGN_OR_RETURN(std::vector<OperatorIR*> containing_ops, ContainingOperators());
  if (!containing_ops.size()) {
    return CreateIRNodeError(
        "Got no containing operators for $0 when looking up referenced operator.", DebugString());
  }
  // While the column may be contained by multiple operators, it must always originate from the same
  // dataframe.
  OperatorIR* referenced = containing_ops[0]->parents()[container_op_parent_idx_];
  for (OperatorIR* containing_op : containing_ops) {
    DCHECK_EQ(referenced, containing_op->parents()[container_op_parent_idx_]);
  }
  return referenced;
}

Status ColumnIR::CopyFromNode(const IRNode* source,
                              absl::flat_hash_map<const IRNode*, IRNode*>* copied_nodes_map) {
  PL_RETURN_IF_ERROR(ExpressionIR::CopyFromNode(source, copied_nodes_map));
  const ColumnIR* column = static_cast<const ColumnIR*>(source);
  col_name_ = column->col_name_;
  col_name_set_ = column->col_name_set_;
  evaluated_data_type_ = column->evaluated_data_type_;
  is_data_type_evaluated_ = column->is_data_type_evaluated_;
  container_op_parent_idx_ = column->container_op_parent_idx_;
  container_op_parent_idx_set_ = column->container_op_parent_idx_set_;
  return Status::OK();
}

Status ColumnIR::CopyFromNodeImpl(const IRNode*, absl::flat_hash_map<const IRNode*, IRNode*>*) {
  return Status::OK();
}

Status ColumnIR::ResolveType(CompilerState* /* compiler_state */,
                             const std::vector<TypePtr>& parent_types) {
  DCHECK(container_op_parent_idx_set_);
  DCHECK_LT(container_op_parent_idx_, parent_types.size());
  auto parent_table = std::static_pointer_cast<TableType>(parent_types[container_op_parent_idx_]);
  PL_ASSIGN_OR_RETURN(auto type_, parent_table->GetColumnType(col_name_));
  return SetResolvedType(type_->Copy());
}

}  // namespace planner
}  // namespace carnot
}  // namespace px