#include "src/carnot/compiler/objects/dataframe.h"
#include "src/carnot/compiler/ir/ast_utils.h"
#include "src/carnot/compiler/objects/metadata_object.h"
#include "src/carnot/compiler/objects/none_object.h"

namespace pl {
namespace carnot {
namespace compiler {
Dataframe::Dataframe(OperatorIR* op) : QLObject(DataframeType, op), op_(op) {
  CHECK(op != nullptr) << "Bad argument in Dataframe constructor.";
  /**
   * # Equivalent to the python method method syntax:
   * def merge(self, right, how, left_on, right_on, suffixes=('_x', '_y')):
   *     ...
   */
  std::shared_ptr<FuncObject> mergefn(new FuncObject(
      kMergeOpId, {"right", "how", "left_on", "right_on", "suffixes"},
      {{"suffixes", "('_x', '_y')"}},
      /* has_variable_len_kwargs */ false,
      std::bind(&JoinHandler::Eval, this, std::placeholders::_1, std::placeholders::_2)));
  AddMethod(kMergeOpId, mergefn);

  /**
   * # Equivalent to the python method method syntax:
   * def agg(self, **kwargs):
   *     ...
   */
  // TODO(philkuz) (PL-1128) re-enable this when new agg syntax is supported.
  // std::shared_ptr<FuncObject> aggfn(new FuncObject(
  //     kBlockingAggOpId, {}, {},
  //     /* has_variable_len_kwargs */ true,
  //     std::bind(&AggHandler::Eval, this, std::placeholders::_1, std::placeholders::_2)));
  // AddMethod(kBlockingAggOpId, aggfn);

  /**
   * # Equivalent to the python method method syntax:
   * def range(self, start, stop=plc.now()):
   *     ...
   */
  std::shared_ptr<FuncObject> rangefn(new FuncObject(
      kRangeOpId, {"start", "stop"}, {{"stop", "plc.now()"}},
      /* has_variable_len_kwargs */ false,
      std::bind(&RangeHandler::Eval, this, std::placeholders::_1, std::placeholders::_2)));
  AddMethod(kRangeOpId, rangefn);

  // TODO(philkuz) (PL-1036) remove this upon availability of new syntax.
  /**
   * # Equivalent to the python method method syntax:
   * def map(self, fn):
   *     ...
   */
  std::shared_ptr<FuncObject> mapfn(new FuncObject(
      kMapOpId, {"fn"}, {}, /* has_variable_len_kwargs */ false,
      std::bind(&OldMapHandler::Eval, this, std::placeholders::_1, std::placeholders::_2)));
  AddMethod(kMapOpId, mapfn);

  // TODO(philkuz) (PL-1036) remove this upon availability of new syntax.
  /**
   * # Equivalent to the python method method syntax:
   * def drop(self, fn):
   *     ...
   */
  std::shared_ptr<FuncObject> dropfn(new FuncObject(
      kDropOpId, {"columns"}, {}, /* has_kwargs */ false,
      std::bind(&DropHandler::Eval, this, std::placeholders::_1, std::placeholders::_2)));
  AddMethod(kDropOpId, dropfn);

  // TODO(philkuz) (PL-1038) remove this upon availability of new syntax.
  /**
   * # Equivalent to the python method method syntax:
   * def filter(self, fn):
   *     ...
   */
  std::shared_ptr<FuncObject> filterfn(new FuncObject(
      kFilterOpId, {"fn"}, {}, /* has_variable_len_kwargs */ false,
      std::bind(&OldFilterHandler::Eval, this, std::placeholders::_1, std::placeholders::_2)));
  AddMethod(kFilterOpId, filterfn);

  /**
   * # Equivalent to the python method method syntax:
   * def limit(self, rows):
   *     ...
   */
  std::shared_ptr<FuncObject> limitfn(new FuncObject(
      kLimitOpId, {"rows"}, {}, /* has_variable_len_kwargs */ false,
      std::bind(&LimitHandler::Eval, this, std::placeholders::_1, std::placeholders::_2)));
  AddMethod(kLimitOpId, limitfn);

  // TODO(philkuz) (PL-1128) disable this when new agg syntax is supported.
  /**
   * # Equivalent to the python method method syntax:
   * def agg(self, by, fn):
   *     ...
   */
  std::shared_ptr<FuncObject> aggfn(new FuncObject(
      kBlockingAggOpId, {"by", "fn"}, {{"by", "lambda x : []"}},
      /* has_variable_len_kwargs */ false,
      std::bind(&OldAggHandler::Eval, this, std::placeholders::_1, std::placeholders::_2)));
  AddMethod(kBlockingAggOpId, aggfn);

  // TODO(philkuz) (PL-1128) disable this when new result syntax is supported.
  /**
   * # Equivalent to the python method method syntax:
   * def result(self, name):
   *     ...
   */
  std::shared_ptr<FuncObject> old_sink_fn(new FuncObject(
      kSinkOpId, {"name"}, {},
      /* has_variable_len_kwargs */ false,
      std::bind(&OldResultHandler::Eval, this, std::placeholders::_1, std::placeholders::_2)));
  AddMethod(kSinkOpId, old_sink_fn);

  // TODO(philkuz) (PL-1086) update when new range_agg behavior comes into play.
  /**
   * # Equivalent to the python method method syntax:
   * def range_agg(self, by, fn, size):
   *     ...
   */
  std::shared_ptr<FuncObject> old_range_agg_fn(new FuncObject(
      kRangeAggOpId, {"by", "fn", "size"}, {},
      /* has_variable_len_kwargs */ false,
      std::bind(&OldRangeAggHandler::Eval, this, std::placeholders::_1, std::placeholders::_2)));
  AddMethod(kRangeAggOpId, old_range_agg_fn);

  /**
   *
   * # Equivalent to the python method method syntax:
   * def __getitem__(self, key):
   *     ...
   *
   * # It's important to note that this is added as a subscript method instead.
   */
  std::shared_ptr<FuncObject> subscript_fn(new FuncObject(
      kSubscriptMethodName, {"key"}, {},
      /* has_variable_len_kwargs */ false,
      std::bind(&SubscriptHandler::Eval, this, std::placeholders::_1, std::placeholders::_2)));
  AddSubscriptMethod(subscript_fn);

  std::shared_ptr<FuncObject> group_by_fn(new FuncObject(
      kGroupByOpId, {"by"}, {},
      /* has_variable_len_kwargs */ false,
      std::bind(&GroupByHandler::Eval, this, std::placeholders::_1, std::placeholders::_2)));
  AddMethod(kGroupByOpId, group_by_fn);
}

bool Dataframe::HasAttributeImpl(const std::string& name) const {
  if (name == kMetadataAttrName) {
    return true;
  }
  // Leaving room for other attributes here.
  return false;
}

StatusOr<QLObjectPtr> Dataframe::GetAttributeImpl(const pypa::AstPtr& ast,
                                                  const std::string& name) const {
  // If this gets to this point, should fail here.
  DCHECK(HasAttributeImpl(name));

  if (name == kMetadataAttrName) {
    return MetadataObject::Create(op());
  }

  // Shouldn't ever be hit, but will appear here anyways.
  return CreateAstError(ast, "'$0' object has no attribute '$1'", name);
}

StatusOr<QLObjectPtr> JoinHandler::Eval(Dataframe* df, const pypa::AstPtr& ast,
                                        const ParsedArgs& args) {
  // GetArg returns non-nullptr or errors out in Debug mode. No need
  // to check again.
  IRNode* right_node = args.GetArg("right");
  IRNode* how_node = args.GetArg("how");
  IRNode* left_on_node = args.GetArg("left_on");
  IRNode* right_on_node = args.GetArg("right_on");
  IRNode* suffixes_node = args.GetArg("suffixes");
  if (!Match(right_node, Operator())) {
    return right_node->CreateIRNodeError("'right' must be an operator, got $0",
                                         right_node->type_string());
  }
  OperatorIR* right = static_cast<OperatorIR*>(right_node);

  if (!Match(how_node, String())) {
    return how_node->CreateIRNodeError("'how' must be a string, got $0", how_node->type_string());
  }
  std::string how_type = static_cast<StringIR*>(how_node)->str();

  PL_ASSIGN_OR_RETURN(std::vector<ColumnIR*> left_on_cols, ProcessCols(left_on_node, "left_on", 0));
  PL_ASSIGN_OR_RETURN(std::vector<ColumnIR*> right_on_cols,
                      ProcessCols(right_on_node, "right_on", 1));

  // TODO(philkuz) consider using a struct instead of a vector because it's a fixed size.
  if (!Match(suffixes_node, CollectionWithChildren(String()))) {
    return suffixes_node->CreateIRNodeError(
        "'suffixes' must be a tuple with 2 strings for the left and right suffixes. Received $0",
        suffixes_node->type_string());
  }

  PL_ASSIGN_OR_RETURN(std::vector<std::string> suffix_strs,
                      ParseStringsFromCollection(static_cast<ListIR*>(suffixes_node)));
  if (suffix_strs.size() != 2) {
    return suffixes_node->CreateIRNodeError(
        "'suffixes' must be a tuple with 2 elements. Received $0", suffix_strs.size());
  }

  PL_ASSIGN_OR_RETURN(JoinIR * join_op, df->graph()->CreateNode<JoinIR>(
                                            ast, std::vector<OperatorIR*>{df->op(), right},
                                            how_type, left_on_cols, right_on_cols, suffix_strs));
  return StatusOr(std::make_shared<Dataframe>(join_op));
}

StatusOr<std::vector<ColumnIR*>> JoinHandler::ProcessCols(IRNode* node, std::string arg_name,
                                                          int64_t parent_index) {
  DCHECK(node != nullptr);
  IR* graph = node->graph_ptr();
  if (Match(node, ListWithChildren(String()))) {
    auto list = static_cast<ListIR*>(node);
    std::vector<ColumnIR*> columns(list->children().size());
    for (const auto& [idx, node] : Enumerate(list->children())) {
      StringIR* str = static_cast<StringIR*>(node);
      PL_ASSIGN_OR_RETURN(ColumnIR * col,
                          graph->CreateNode<ColumnIR>(str->ast_node(), str->str(), parent_index));
      columns[idx] = col;
    }
    return columns;
  } else if (!Match(node, String())) {
    return node->CreateIRNodeError("'$0' must be a label or a list of labels", arg_name);
  }
  StringIR* str = static_cast<StringIR*>(node);
  PL_ASSIGN_OR_RETURN(ColumnIR * col,
                      graph->CreateNode<ColumnIR>(str->ast_node(), str->str(), parent_index));
  return std::vector<ColumnIR*>{col};
}

StatusOr<QLObjectPtr> AggHandler::Eval(Dataframe* df, const pypa::AstPtr& ast,
                                       const ParsedArgs& args) {
  // converts the mapping of args.kwargs into ColExpressionvector
  ColExpressionVector aggregate_expressions;
  for (const auto& [name, expr] : args.kwargs()) {
    if (!Match(expr, Tuple())) {
      return expr->CreateIRNodeError("Expected '$0' kwarg argument to be a tuple, not $1",
                                     Dataframe::kBlockingAggOpId, expr->type_string());
    }
    PL_ASSIGN_OR_RETURN(FuncIR * parsed_expr,
                        ParseNameTuple(df->graph(), static_cast<TupleIR*>(expr)));
    aggregate_expressions.push_back({name, parsed_expr});
  }

  PL_ASSIGN_OR_RETURN(BlockingAggIR * agg_op,
                      df->graph()->CreateNode<BlockingAggIR>(
                          ast, df->op(), std::vector<ColumnIR*>{}, aggregate_expressions));
  return StatusOr(std::make_shared<Dataframe>(agg_op));
}

StatusOr<FuncIR*> AggHandler::ParseNameTuple(IR* ir, TupleIR* tuple) {
  DCHECK_EQ(tuple->children().size(), 2UL);
  IRNode* childone = tuple->children()[0];
  IRNode* childtwo = tuple->children()[1];
  if (!Match(childone, String())) {
    return childone->CreateIRNodeError("Expected 'str' for first tuple argument. Received '$0'",
                                       childone->type_string());
  }

  if (!Match(childtwo, Func())) {
    return childtwo->CreateIRNodeError("Expected 'func' for second tuple argument. Received '$0'",
                                       childtwo->type_string());
  }

  std::string argcol_name = static_cast<StringIR*>(childone)->str();
  FuncIR* func = static_cast<FuncIR*>(childtwo);
  // The function should be specified as a single function by itself.
  // This could change in the future.
  if (func->args().size() != 0) {
    return func->CreateIRNodeError("Expected function to not have specified arguments");
  }
  // parent_op_idx is 0 because we only have one parent for an aggregate.
  PL_ASSIGN_OR_RETURN(ColumnIR * argcol, ir->CreateNode<ColumnIR>(childone->ast_node(), argcol_name,
                                                                  /* parent_op_idx */ 0));
  PL_RETURN_IF_ERROR(func->AddArg(argcol));

  // Delete tuple id.
  PL_RETURN_IF_ERROR(ir->DeleteNode(tuple->id()));
  return func;
}

StatusOr<QLObjectPtr> DropHandler::Eval(Dataframe* df, const pypa::AstPtr& ast,
                                        const ParsedArgs& args) {
  IRNode* columns_arg = args.GetArg("columns");
  if (!Match(columns_arg, List())) {
    return columns_arg->CreateIRNodeError(
        "Expected '$0' kwarg argument 'columns' to be a list, not $1", Dataframe::kDropOpId,
        columns_arg->type_string());
  }
  ListIR* columns_list = static_cast<ListIR*>(columns_arg);
  PL_ASSIGN_OR_RETURN(std::vector<std::string> columns, ParseStringsFromCollection(columns_list));

  PL_ASSIGN_OR_RETURN(DropIR * drop_op, df->graph()->CreateNode<DropIR>(ast, df->op(), columns));
  PL_RETURN_IF_ERROR(df->graph()->DeleteNodeAndChildren(columns_list->id()));
  return StatusOr(std::make_shared<Dataframe>(drop_op));
}

StatusOr<QLObjectPtr> RangeHandler::Eval(Dataframe* df, const pypa::AstPtr& ast,
                                         const ParsedArgs& args) {
  IRNode* start_repr = args.GetArg("start");
  IRNode* stop_repr = args.GetArg("stop");
  if (!Match(start_repr, Expression())) {
    return start_repr->CreateIRNodeError("'start' must be an expression");
  }

  if (!Match(stop_repr, Expression())) {
    return stop_repr->CreateIRNodeError("'stop' must be an expression");
  }

  ExpressionIR* start_expr = static_cast<ExpressionIR*>(start_repr);
  ExpressionIR* stop_expr = static_cast<ExpressionIR*>(stop_repr);

  PL_ASSIGN_OR_RETURN(RangeIR * range_op,
                      df->graph()->CreateNode<RangeIR>(ast, df->op(), start_expr, stop_expr));
  return StatusOr(std::make_shared<Dataframe>(range_op));
}

/**
 * @brief Returns error if the lambda doens't have the number of parents specified.
 *
 * @param lambda
 * @return Status
 */
Status VerifyLambda(LambdaIR* lambda, const std::string& arg_name, int64_t num_parents,
                    bool should_have_dict_body) {
  // Check to see if expectations matches the lambda reality.
  if (should_have_dict_body != lambda->HasDictBody()) {
    if (should_have_dict_body) {
      return lambda->CreateIRNodeError("'$0' argument error, lambda must have a dictionary body",
                                       arg_name);
    }

    return lambda->CreateIRNodeError("'$0' argument error, lambda cannot have a dictionary body",
                                     arg_name);
  }

  if (lambda->number_of_parents() != num_parents) {
    std::string parent_name = "parents";
    if (num_parents == 1) {
      parent_name = "parent";
    }
    return lambda->CreateIRNodeError("'$0' operator expects $0 $2, received $1", num_parents,
                                     lambda->number_of_parents(), parent_name);
  }

  return Status::OK();
}

StatusOr<QLObjectPtr> OldMapHandler::Eval(Dataframe* df, const pypa::AstPtr& ast,
                                          const ParsedArgs& args) {
  IRNode* lambda_func = args.GetArg("fn");
  if (!Match(lambda_func, Lambda())) {
    return lambda_func->CreateIRNodeError("'fn' must be a lambda");
  }
  LambdaIR* lambda = static_cast<LambdaIR*>(lambda_func);
  PL_RETURN_IF_ERROR(VerifyLambda(lambda, "fn", 1, /* should_have_dict_body */ true));

  PL_ASSIGN_OR_RETURN(MapIR * map_op,
                      df->graph()->CreateNode<MapIR>(ast, df->op(), lambda->col_exprs()));
  // Delete the lambda.
  PL_RETURN_IF_ERROR(df->graph()->DeleteNode(lambda->id()));
  return StatusOr(std::make_shared<Dataframe>(map_op));
}

StatusOr<QLObjectPtr> OldFilterHandler::Eval(Dataframe* df, const pypa::AstPtr& ast,
                                             const ParsedArgs& args) {
  IRNode* lambda_func = args.GetArg("fn");
  if (!Match(lambda_func, Lambda())) {
    return lambda_func->CreateIRNodeError("'fn' must be a lambda");
  }

  LambdaIR* lambda = static_cast<LambdaIR*>(lambda_func);
  PL_RETURN_IF_ERROR(VerifyLambda(lambda, "fn", 1, /* should_have_dict_body */ false));

  // Have to remove the edges from the Lambda
  PL_ASSIGN_OR_RETURN(ExpressionIR * expr, lambda->GetDefaultExpr());

  PL_ASSIGN_OR_RETURN(FilterIR * filter_op, df->graph()->CreateNode<FilterIR>(ast, df->op(), expr));
  // Delete the lambda.
  PL_RETURN_IF_ERROR(df->graph()->DeleteNode(lambda->id()));
  return StatusOr(std::make_shared<Dataframe>(filter_op));
}

StatusOr<QLObjectPtr> LimitHandler::Eval(Dataframe* df, const pypa::AstPtr& ast,
                                         const ParsedArgs& args) {
  // TODO(philkuz) (PL-1161) Add support for compile time evaluation of Limit argument.
  IRNode* rows_node = args.GetArg("rows");
  if (!Match(rows_node, Int())) {
    return rows_node->CreateIRNodeError("'rows' must be an int");
  }
  int64_t limit_value = static_cast<IntIR*>(rows_node)->val();

  PL_ASSIGN_OR_RETURN(LimitIR * limit_op,
                      df->graph()->CreateNode<LimitIR>(ast, df->op(), limit_value));
  // Delete the integer node.
  PL_RETURN_IF_ERROR(df->graph()->DeleteNode(rows_node->id()));
  return StatusOr(std::make_shared<Dataframe>(limit_op));
}

StatusOr<QLObjectPtr> OldAggHandler::Eval(Dataframe* df, const pypa::AstPtr& ast,
                                          const ParsedArgs& args) {
  IRNode* by_func = args.GetArg("by");
  IRNode* fn_func = args.GetArg("fn");
  if (!Match(by_func, Lambda())) {
    return by_func->CreateIRNodeError("'by' must be a lambda");
  }
  if (!Match(fn_func, Lambda())) {
    return fn_func->CreateIRNodeError("'fn' must be a lambda");
  }
  LambdaIR* fn = static_cast<LambdaIR*>(fn_func);
  PL_RETURN_IF_ERROR(VerifyLambda(fn, "fn", 1, /* should_have_dict_body */ true));

  LambdaIR* by = static_cast<LambdaIR*>(by_func);
  PL_RETURN_IF_ERROR(VerifyLambda(by, "by", 1, /* should_have_dict_body */ false));

  // Have to remove the edges from the by lambda.
  PL_ASSIGN_OR_RETURN(ExpressionIR * by_expr, by->GetDefaultExpr());
  PL_ASSIGN_OR_RETURN(std::vector<ColumnIR*> groups, SetupGroups(by_expr));

  PL_ASSIGN_OR_RETURN(BlockingAggIR * agg_op, df->graph()->CreateNode<BlockingAggIR>(
                                                  ast, df->op(), groups, fn->col_exprs()));
  // Delete the by.
  PL_RETURN_IF_ERROR(df->graph()->DeleteNode(by->id()));
  PL_RETURN_IF_ERROR(df->graph()->DeleteNode(fn->id()));
  return StatusOr(std::make_shared<Dataframe>(agg_op));
}

StatusOr<std::vector<ColumnIR*>> OldAggHandler::SetupGroups(ExpressionIR* group_by_expr) {
  std::vector<ColumnIR*> groups;
  if (Match(group_by_expr, ListWithChildren(ColumnNode()))) {
    for (ExpressionIR* child : static_cast<ListIR*>(group_by_expr)->children()) {
      groups.push_back(static_cast<ColumnIR*>(child));
    }
    PL_RETURN_IF_ERROR(group_by_expr->graph_ptr()->DeleteNode(group_by_expr->id()));
  } else if (Match(group_by_expr, ColumnNode())) {
    groups.push_back(static_cast<ColumnIR*>(group_by_expr));
  } else {
    return group_by_expr->CreateIRNodeError(
        "'by' lambda must contain a column or a list of columns");
  }
  return groups;
}

StatusOr<QLObjectPtr> OldJoinHandler::Eval(Dataframe* df, const pypa::AstPtr& ast,
                                           const ParsedArgs& args) {
  IRNode* right_node = args.GetArg("right");
  IRNode* type_node = args.GetArg("type");
  IRNode* cond_node = args.GetArg("cond");
  IRNode* cols_node = args.GetArg("cols");

  if (!Match(right_node, Operator())) {
    return right_node->CreateIRNodeError("'right' must be a Dataframe");
  }
  if (!Match(cond_node, Lambda())) {
    return cond_node->CreateIRNodeError("'cond' must be a lambda");
  }
  if (!Match(cols_node, Lambda())) {
    return cols_node->CreateIRNodeError("'cols' must be a lambda");
  }
  if (!Match(type_node, String())) {
    return type_node->CreateIRNodeError("'type' must be a str");
  }

  OperatorIR* right = static_cast<OperatorIR*>(right_node);

  LambdaIR* cols = static_cast<LambdaIR*>(cols_node);
  PL_RETURN_IF_ERROR(VerifyLambda(cols, "cols", 2, /* should_have_dict_body */ true));

  LambdaIR* cond = static_cast<LambdaIR*>(cond_node);
  PL_RETURN_IF_ERROR(VerifyLambda(cond, "cond", 2, /* should_have_dict_body */ false));

  std::string how_str = static_cast<StringIR*>(type_node)->str();
  PL_RETURN_IF_ERROR(df->graph()->DeleteNode(type_node->id()));

  std::vector<ColumnIR*> columns;
  std::vector<std::string> column_names;
  // Have to remove the edges from the fn lambda.
  for (const ColumnExpression& mapped_expression : cols->col_exprs()) {
    ExpressionIR* expr = mapped_expression.node;
    if (!Match(expr, ColumnNode())) {
      return expr->CreateIRNodeError("'cols' can only have columns");
    }
    column_names.push_back(mapped_expression.name);
    columns.push_back(static_cast<ColumnIR*>(expr));
  }

  // Have to remove the edges from the by lambda.
  PL_ASSIGN_OR_RETURN(ExpressionIR * cond_expr, cond->GetDefaultExpr());
  PL_ASSIGN_OR_RETURN(JoinIR::EqConditionColumns eq_condition, JoinIR::ParseCondition(cond_expr));

  PL_ASSIGN_OR_RETURN(JoinIR * join_op, df->graph()->CreateNode<JoinIR>(
                                            ast, std::vector<OperatorIR*>{df->op(), right}, how_str,
                                            eq_condition.left_on_cols, eq_condition.right_on_cols,
                                            std::vector<std::string>{}));
  PL_RETURN_IF_ERROR(join_op->SetOutputColumns(column_names, columns));
  // Delete the lambdas.
  PL_RETURN_IF_ERROR(df->graph()->DeleteNode(cond_node->id()));
  PL_RETURN_IF_ERROR(df->graph()->DeleteNode(cols_node->id()));
  return StatusOr(std::make_shared<Dataframe>(join_op));
}

StatusOr<QLObjectPtr> OldResultHandler::Eval(Dataframe* df, const pypa::AstPtr& ast,
                                             const ParsedArgs& args) {
  IRNode* name_node = args.GetArg("name");
  if (!Match(name_node, String())) {
    return name_node->CreateIRNodeError("'name' must be a str");
  }
  std::string name = static_cast<StringIR*>(name_node)->str();
  PL_ASSIGN_OR_RETURN(MemorySinkIR * sink_op, df->graph()->CreateNode<MemorySinkIR>(
                                                  ast, df->op(), name, std::vector<std::string>{}));
  return StatusOr(std::make_shared<NoneObject>(sink_op));
}

StatusOr<QLObjectPtr> OldRangeAggHandler::Eval(Dataframe* df, const pypa::AstPtr& ast,
                                               const ParsedArgs& args) {
  // Creates the Map->Agg sequence that mimics RangeAgg.
  IRNode* by_func = args.GetArg("by");
  IRNode* fn_func = args.GetArg("fn");
  IRNode* size_node = args.GetArg("size");

  if (!Match(by_func, Lambda())) {
    return by_func->CreateIRNodeError("'by' must be a lambda");
  }
  if (!Match(fn_func, Lambda())) {
    return fn_func->CreateIRNodeError("'fn' must be a lambda");
  }
  if (!Match(size_node, Int())) {
    return size_node->CreateIRNodeError("'size' must be a lambda");
  }

  LambdaIR* fn = static_cast<LambdaIR*>(fn_func);
  PL_RETURN_IF_ERROR(VerifyLambda(fn, "fn", 1, /* should_have_dict_body */ true));

  LambdaIR* by = static_cast<LambdaIR*>(by_func);
  PL_RETURN_IF_ERROR(VerifyLambda(by, "by", 1, /* should_have_dict_body */ false));

  IntIR* size = static_cast<IntIR*>(size_node);

  PL_ASSIGN_OR_RETURN(ExpressionIR * by_expr, by->GetDefaultExpr());
  PL_ASSIGN_OR_RETURN(std::vector<ColumnIR*> groups, OldAggHandler::SetupGroups(by_expr));

  if (groups.size() != 1) {
    return by_expr->CreateIRNodeError("expected 1 column to group by, received $0", groups.size());
  }

  ColumnIR* range_agg_col = groups[0];

  PL_ASSIGN_OR_RETURN(FuncIR * group_expression,
                      MakeRangeAggGroupExpression(range_agg_col, size, ast, df->graph()));

  ColExpressionVector map_exprs{{"group", group_expression}};
  // TODO(philkuz/nserrino) when D2570 lands, add copy columns as init arg instead of doing this.
  for (const auto& name : fn->expected_column_names()) {
    PL_ASSIGN_OR_RETURN(ColumnIR * col_node,
                        df->graph()->CreateNode<ColumnIR>(ast, name, /* parent_op_idx */ 0));
    map_exprs.push_back({name, col_node});
  }

  PL_ASSIGN_OR_RETURN(MapIR * map, df->graph()->CreateNode<MapIR>(ast, df->op(), map_exprs));

  // Make the Blocking Agg prerequisite nodes.

  PL_ASSIGN_OR_RETURN(ColumnIR * agg_group_by_col,
                      df->graph()->CreateNode<ColumnIR>(ast, "group", /*parent_op_idx*/ 0));

  PL_ASSIGN_OR_RETURN(BlockingAggIR * agg,
                      df->graph()->CreateNode<BlockingAggIR>(
                          ast, map, std::vector<ColumnIR*>{agg_group_by_col}, fn->col_exprs()));

  PL_RETURN_IF_ERROR(df->graph()->DeleteNode(by->id()));
  PL_RETURN_IF_ERROR(df->graph()->DeleteNode(fn->id()));
  return StatusOr(std::make_shared<Dataframe>(agg));
}

StatusOr<FuncIR*> OldRangeAggHandler::MakeRangeAggGroupExpression(ColumnIR* range_agg_col,
                                                                  IntIR* size_expr,
                                                                  const pypa::AstPtr& ast,
                                                                  IR* graph) {
  auto op_map_iter = FuncIR::op_map.find("%");
  DCHECK(op_map_iter != FuncIR::op_map.end());
  FuncIR::Op mod_op = op_map_iter->second;

  PL_ASSIGN_OR_RETURN(
      FuncIR * mod_ir_node,
      graph->CreateNode<FuncIR>(ast, mod_op, std::vector<ExpressionIR*>{range_agg_col, size_expr}));

  PL_ASSIGN_OR_RETURN(
      ColumnIR * range_agg_col_copy,
      graph->CreateNode<ColumnIR>(ast, range_agg_col->col_name(), /* parent_op_idx */ 0));

  op_map_iter = FuncIR::op_map.find("-");
  DCHECK(op_map_iter != FuncIR::op_map.end());
  FuncIR::Op sub_op = op_map_iter->second;

  // pl.subtract(by_col, pl.mod(by_col, size)).
  PL_ASSIGN_OR_RETURN(
      FuncIR * sub_ir_node,
      graph->CreateNode<FuncIR>(ast, sub_op,
                                std::vector<ExpressionIR*>{range_agg_col_copy, mod_ir_node}));

  return sub_ir_node;
}

StatusOr<QLObjectPtr> SubscriptHandler::Eval(Dataframe* df, const pypa::AstPtr& ast,
                                             const ParsedArgs& args) {
  IRNode* key = args.GetArg("key");
  if (!key->IsExpression()) {
    return key->CreateIRNodeError("subscript argument must have an expression. '$0' not allowed",
                                  key->type_string());
  }
  if (Match(key, List())) {
    return EvalKeep(df, ast, static_cast<ListIR*>(key));
  }
  return EvalFilter(df, ast, static_cast<ExpressionIR*>(key));
}

StatusOr<QLObjectPtr> SubscriptHandler::EvalFilter(Dataframe* df, const pypa::AstPtr& ast,
                                                   ExpressionIR* expr) {
  PL_ASSIGN_OR_RETURN(FilterIR * filter_op, df->graph()->CreateNode<FilterIR>(ast, df->op(), expr));
  return StatusOr(std::make_shared<Dataframe>(filter_op));
}

StatusOr<QLObjectPtr> SubscriptHandler::EvalKeep(Dataframe* df, const pypa::AstPtr& ast,
                                                 ListIR* key) {
  PL_ASSIGN_OR_RETURN(std::vector<std::string> keep_column_names, ParseStringsFromCollection(key));

  ColExpressionVector keep_exprs;
  for (const auto& col_name : keep_column_names) {
    // parent_op_idx is 0 because we only have one parent for a map.
    PL_ASSIGN_OR_RETURN(ColumnIR * keep_col,
                        df->graph()->CreateNode<ColumnIR>(ast, col_name, /* parent_op_idx */ 0));
    keep_exprs.emplace_back(col_name, keep_col);
  }

  PL_ASSIGN_OR_RETURN(MapIR * map_op, df->graph()->CreateNode<MapIR>(ast, df->op(), keep_exprs));
  // TODO(nserrino): Refactor this once lambda maps are deprecated.
  // Technically not needed but here for explicitness until the refactor.
  map_op->set_keep_input_columns(false);
  return StatusOr(std::make_shared<Dataframe>(map_op));
}

StatusOr<QLObjectPtr> GroupByHandler::Eval(Dataframe* df, const pypa::AstPtr& ast,
                                           const ParsedArgs& args) {
  IRNode* by = args.GetArg("by");

  PL_ASSIGN_OR_RETURN(std::vector<ColumnIR*> groups, ParseByFunction(by));
  PL_ASSIGN_OR_RETURN(GroupByIR * group_by_op,
                      df->graph()->CreateNode<GroupByIR>(ast, df->op(), groups));
  return StatusOr(std::make_shared<Dataframe>(group_by_op));
}

StatusOr<std::vector<ColumnIR*>> GroupByHandler::ParseByFunction(IRNode* by) {
  if (!Match(by, ListWithChildren(String())) && !Match(by, String())) {
    return by->CreateIRNodeError("'by' expected string or list of strings");
  } else if (Match(by, String())) {
    PL_ASSIGN_OR_RETURN(ColumnIR * col,
                        by->graph_ptr()->CreateNode<ColumnIR>(
                            by->ast_node(), static_cast<StringIR*>(by)->str(), /* parent_idx */ 0));
    return std::vector<ColumnIR*>{col};
  }

  PL_ASSIGN_OR_RETURN(std::vector<std::string> column_names,
                      ParseStringsFromCollection(static_cast<ListIR*>(by)));
  std::vector<ColumnIR*> columns;
  for (const auto& col_name : column_names) {
    PL_ASSIGN_OR_RETURN(ColumnIR * col,
                        by->graph_ptr()->CreateNode<ColumnIR>(by->ast_node(), col_name,
                                                              /* parent_idx */ 0));
    columns.push_back(col);
  }
  return columns;
}

}  // namespace compiler
}  // namespace carnot
}  // namespace pl
