#include <DB/Core/Block.h>
#include <DB/Storages/ColumnDefault.h>
#include <DB/Interpreters/ExpressionAnalyzer.h>
#include <DB/Interpreters/ExpressionActions.h>
#include <DB/Interpreters/evaluateMissingDefaults.h>
#include <utility>


namespace DB
{

void evaluateMissingDefaults(Block & block,
    const NamesAndTypesList & required_columns,
	const ColumnDefaults & column_defaults,
	const Context & context)
{
	if (column_defaults.empty())
		return;

	ASTPtr default_expr_list{std::make_unique<ASTExpressionList>().release()};

	for (const auto & column : required_columns)
	{
		if (block.has(column.name))
			continue;

		const auto it = column_defaults.find(column.name);

		/// expressions must be cloned to prevent modification by the ExpressionAnalyzer
		if (it != column_defaults.end())
			default_expr_list->children.emplace_back(
				setAlias(it->second.expression->clone(), it->first));
	}

	/// nothing to evaluate
	if (default_expr_list->children.empty())
		return;

	/** ExpressionAnalyzer eliminates "unused" columns, in order to ensure their safety
	  * we are going to operate on a copy instead of the original block */
	Block copy_block{block};
	/// evaluate default values for defaulted columns
	ExpressionAnalyzer{default_expr_list, context, {}, required_columns}.getActions(true)->execute(copy_block);

	/// move evaluated columns to the original block, materializing them at the same time
	for (auto & column_name_type : copy_block.getColumns())
	{
		if (auto converted = column_name_type.column->convertToFullColumnIfConst())
			column_name_type.column = converted;

		block.insert(std::move(column_name_type));
	}
}

}
