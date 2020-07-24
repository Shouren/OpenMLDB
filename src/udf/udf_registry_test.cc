/*-------------------------------------------------------------------------
 * Copyright (C) 2019, 4paradigm
 * udf_registry_test.cc
 *
 * Author: chenjing
 * Date: 2019/11/26
 *--------------------------------------------------------------------------
 **/
#include "udf/udf_registry.h"
#include <gtest/gtest.h>
#include "codegen/context.h"

namespace fesql {
namespace udf {

using fesql::base::Status;
using fesql::codegen::CodeGenContext;
using fesql::codegen::NativeValue;
using fesql::node::ExprAnalysisContext;
using fesql::node::ExprNode;
using fesql::node::NodeManager;
using fesql::node::TypeNode;

class UDFRegistryTest : public ::testing::Test {};

template <typename... LiteralArgTypes>
const node::FnDefNode* GetFnDef(UDFLibrary* lib, const std::string& name,
                                node::NodeManager* nm,
                                const node::SQLNode* over = nullptr) {
    std::vector<node::TypeNode*> arg_types(
        {DataTypeTrait<LiteralArgTypes>::to_type_node(nm)...});
    auto arg_list = reinterpret_cast<node::ExprListNode*>(nm->MakeExprList());
    for (size_t i = 0; i < arg_types.size(); ++i) {
        std::string arg_name = "arg_" + std::to_string(i);
        auto expr = nm->MakeExprIdNode(arg_name, node::ExprIdNode::GetNewId());
        expr->SetOutputType(arg_types[i]);
        arg_list->AddChild(expr);
    }
    node::ExprNode* transformed = nullptr;
    node::ExprAnalysisContext analysis_ctx(nm, nullptr, over != nullptr);
    auto status =
        lib->Transform(name, arg_list, over, &analysis_ctx, &transformed);
    if (!status.isOK()) {
        LOG(WARNING) << status.msg;
        return nullptr;
    }
    if (transformed->GetExprType() != node::kExprCall) {
        LOG(WARNING) << "Resolved result is not call expr";
        return nullptr;
    }
    return reinterpret_cast<node::CallExprNode*>(transformed)->GetFnDef();
}

TEST_F(UDFRegistryTest, test_expr_udf_register) {
    UDFLibrary library;
    node::NodeManager nm;
    auto over = nm.MakeWindowDefNode("w");
    const node::FnDefNode* fn_def;

    // define "add"
    library.RegisterExprUDF("add")
        .allow_window(false)
        .allow_project(true)

        .args<AnyArg, AnyArg>(
            [](UDFResolveContext* ctx, ExprNode* x, ExprNode* y) {
                // extra check logic
                auto ltype = x->GetOutputType();
                auto rtype = y->GetOutputType();
                if (ltype && rtype && !ltype->Equals(rtype)) {
                    ctx->SetError("Left and right known type should equal");
                }
                return ctx->node_manager()->MakeBinaryExprNode(x, y,
                                                               node::kFnOpAdd);
            })

        .args<double, int32_t>(
            [](UDFResolveContext* ctx, ExprNode* x, ExprNode* y) {
                return ctx->node_manager()->MakeBinaryExprNode(x, y,
                                                               node::kFnOpAdd);
            })

        .args<StringRef, AnyArg, bool>(
            [](UDFResolveContext* ctx, ExprNode* x, ExprNode* y, ExprNode* z) {
                return ctx->node_manager()->MakeBinaryExprNode(x, y,
                                                               node::kFnOpAdd);
            });

    // resolve "add"
    // match placeholder
    fn_def = GetFnDef<int32_t, int32_t>(&library, "add", &nm);
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kLambdaDef);

    // allow_window(false)
    fn_def = GetFnDef<int32_t, int32_t>(&library, "add", &nm, over);
    ASSERT_TRUE(fn_def == nullptr);

    // match argument num
    fn_def = GetFnDef<int32_t, int32_t, int32_t>(&library, "add", &nm);
    ASSERT_TRUE(fn_def == nullptr);

    // match but impl logic error
    fn_def = GetFnDef<int32_t, float>(&library, "add", &nm);
    ASSERT_TRUE(fn_def == nullptr);

    // match explicit
    fn_def = GetFnDef<double, int32_t>(&library, "add", &nm);
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kLambdaDef);

    // match with unknown input arg type
    fn_def = GetFnDef<int32_t, AnyArg>(&library, "add", &nm);
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kLambdaDef);

    // match different argument num
    fn_def = GetFnDef<StringRef, int64_t, bool>(&library, "add", &nm);
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kLambdaDef);
}

TEST_F(UDFRegistryTest, test_variadic_expr_udf_register) {
    UDFLibrary library;
    node::NodeManager nm;
    const node::FnDefNode* fn_def;

    // define "join"
    library.RegisterExprUDF("join")
        .args<StringRef, StringRef>(
            [](UDFResolveContext* ctx, ExprNode* x, ExprNode* y) { return x; })

        .args<StringRef, AnyArg>(
            [](UDFResolveContext* ctx, ExprNode* x, ExprNode* y) {
                if (y->GetOutputType()->GetName() != "int32") {
                    ctx->SetError("Join arg type error");
                }
                return y;
            })

        .variadic_args<StringRef>([](UDFResolveContext* ctx, ExprNode* split,
                                     const std::vector<ExprNode*>& other) {
            if (other.size() < 3) {
                ctx->SetError("Join tail args error");
            }
            return split;
        })

        .variadic_args<AnyArg>([](UDFResolveContext* ctx, ExprNode* split,
                                  const std::vector<ExprNode*>& other) {
            if (other.size() < 2) {
                ctx->SetError("Join tail args error with any split");
            }
            return split;
        });

    // resolve "join"
    // illegal arg types
    fn_def = GetFnDef<int32_t, int32_t>(&library, "join", &nm);
    ASSERT_TRUE(fn_def == nullptr);

    // prefer match non-variadic, prefer explicit match
    fn_def = GetFnDef<StringRef, StringRef>(&library, "join", &nm);
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kLambdaDef);

    // prefer match non-variadic, allow placeholder
    fn_def = GetFnDef<StringRef, int32_t>(&library, "join", &nm);
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kLambdaDef);

    // prefer match non-variadic, but placeholder check failed
    fn_def = GetFnDef<StringRef, int64_t>(&library, "join", &nm);
    ASSERT_TRUE(fn_def == nullptr);

    // match variadic, prefer no-placeholder match
    fn_def = GetFnDef<StringRef, bool, bool, bool>(&library, "join", &nm);
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kLambdaDef);

    // match variadic, prefer no-placeholder match, buf impl logic failed
    fn_def = GetFnDef<StringRef, StringRef, StringRef>(&library, "join", &nm);
    ASSERT_TRUE(fn_def == nullptr);

    // match variadic with placeholder
    fn_def = GetFnDef<int16_t, StringRef, StringRef>(&library, "join", &nm);
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kLambdaDef);
}

TEST_F(UDFRegistryTest, test_variadic_expr_udf_register_order) {
    UDFLibrary library;
    node::NodeManager nm;
    const node::FnDefNode* fn_def;

    // define "concat"
    library.RegisterExprUDF("concat")
        .variadic_args<>(
            [](UDFResolveContext* ctx, const std::vector<ExprNode*> other) {
                return ctx->node_manager()->MakeExprIdNode("concat", 0);
            })

        .variadic_args<int32_t>([](UDFResolveContext* ctx, ExprNode* x,
                                   const std::vector<ExprNode*> other) {
            if (other.size() > 2) {
                ctx->SetError("Error");
            }
            return ctx->node_manager()->MakeExprIdNode("concat", 0);
        });

    // resolve "concat"
    // prefer long non-variadic parameters part
    fn_def = GetFnDef<int32_t, StringRef, StringRef>(&library, "concat", &nm);
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kLambdaDef);
    fn_def = GetFnDef<int32_t, bool, bool, bool>(&library, "concat", &nm);
    ASSERT_TRUE(fn_def == nullptr);

    // empty variadic args
    fn_def = GetFnDef<>(&library, "concat", &nm);
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kLambdaDef);
}

TEST_F(UDFRegistryTest, test_external_udf_register) {
    UDFLibrary library;
    node::NodeManager nm;
    auto over = nm.MakeWindowDefNode("w");
    const node::ExternalFnDefNode* fn_def;

    // define "add"
    library.RegisterExternal("add")
        .allow_window(false)
        .allow_project(true)

        .args<AnyArg, AnyArg>("add_any", reinterpret_cast<void*>(0x01))
        .returns<int32_t>()

        .args<double, double>("add_double", reinterpret_cast<void*>(0x02))
        .returns<double>()

        .args<StringRef, AnyArg, bool>("add_string",
                                       reinterpret_cast<void*>(0x03))
        .returns<StringRef>();

    // resolve "add"
    // match placeholder
    fn_def = dynamic_cast<const node::ExternalFnDefNode*>(
        GetFnDef<int32_t, int32_t>(&library, "add", &nm));
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kExternalFnDef);
    ASSERT_EQ("add_any", fn_def->function_name());
    ASSERT_EQ("int32", fn_def->ret_type()->GetName());

    // allow_window(false)
    fn_def = dynamic_cast<const node::ExternalFnDefNode*>(
        GetFnDef<int32_t, int32_t>(&library, "add", &nm, over));
    ASSERT_TRUE(fn_def == nullptr);

    // match argument num
    fn_def = dynamic_cast<const node::ExternalFnDefNode*>(
        GetFnDef<int32_t, int32_t, int32_t>(&library, "add", &nm));
    ASSERT_TRUE(fn_def == nullptr);

    // match explicit
    fn_def = dynamic_cast<const node::ExternalFnDefNode*>(
        GetFnDef<double, double>(&library, "add", &nm));
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kExternalFnDef);
    ASSERT_EQ("add_double", fn_def->function_name());
    ASSERT_EQ("double", fn_def->ret_type()->GetName());

    // match with unknown input arg type
    fn_def = dynamic_cast<const node::ExternalFnDefNode*>(
        GetFnDef<int32_t, AnyArg>(&library, "add", &nm));
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kExternalFnDef);
    ASSERT_EQ("add_any", fn_def->function_name());
    ASSERT_EQ("int32", fn_def->ret_type()->GetName());

    // match different argument num
    fn_def = dynamic_cast<const node::ExternalFnDefNode*>(
        GetFnDef<StringRef, int64_t, bool>(&library, "add", &nm));
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kExternalFnDef);
    ASSERT_EQ("add_string", fn_def->function_name());
    ASSERT_EQ("string", fn_def->ret_type()->GetName());
}

TEST_F(UDFRegistryTest, test_variadic_external_udf_register) {
    UDFLibrary library;
    node::NodeManager nm;
    const node::ExternalFnDefNode* fn_def;

    // define "join"
    library.RegisterExternal("join")
        .args<StringRef, StringRef>("join2", nullptr)
        .returns<StringRef>()

        .args<StringRef, AnyArg>("join22", nullptr)
        .returns<StringRef>()

        .variadic_args<StringRef>("join_many", nullptr)
        .returns<StringRef>()

        .variadic_args<AnyArg>("join_many2", nullptr)
        .returns<StringRef>();

    // resolve "join"
    // prefer match non-variadic, prefer explicit match
    fn_def = dynamic_cast<const node::ExternalFnDefNode*>(
        GetFnDef<StringRef, StringRef>(&library, "join", &nm));
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kExternalFnDef);
    ASSERT_EQ("join2", fn_def->function_name());
    ASSERT_EQ("string", fn_def->ret_type()->GetName());

    // prefer match non-variadic, allow placeholder
    fn_def = dynamic_cast<const node::ExternalFnDefNode*>(
        GetFnDef<StringRef, int32_t>(&library, "join", &nm));
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kExternalFnDef);
    ASSERT_EQ("join22", fn_def->function_name());

    // match variadic, prefer no-placeholder match
    fn_def = dynamic_cast<const node::ExternalFnDefNode*>(
        GetFnDef<StringRef, bool, bool, bool>(&library, "join", &nm));
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kExternalFnDef);
    ASSERT_EQ("join_many", fn_def->function_name());

    // match variadic with placeholder
    fn_def = dynamic_cast<const node::ExternalFnDefNode*>(
        GetFnDef<int16_t, StringRef, StringRef>(&library, "join", &nm));
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kExternalFnDef);
    ASSERT_EQ("join_many2", fn_def->function_name());
}

TEST_F(UDFRegistryTest, test_variadic_external_udf_register_order) {
    UDFLibrary library;
    node::NodeManager nm;
    const node::ExternalFnDefNode* fn_def;

    // define "concat"
    library.RegisterExternal("concat")
        .variadic_args<>("concat0", nullptr)
        .returns<StringRef>()
        .variadic_args<int32_t>("concat1", nullptr)
        .returns<StringRef>();

    // resolve "concat"
    // prefer long non-variadic parameters part
    fn_def = dynamic_cast<const node::ExternalFnDefNode*>(
        GetFnDef<int32_t, StringRef, StringRef>(&library, "concat", &nm));
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kExternalFnDef);
    ASSERT_EQ("concat1", fn_def->function_name());
    ASSERT_EQ(1, fn_def->variadic_pos());

    // empty variadic args
    fn_def = dynamic_cast<const node::ExternalFnDefNode*>(
        GetFnDef<>(&library, "concat", &nm));
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kExternalFnDef);
    ASSERT_EQ("concat0", fn_def->function_name());
    ASSERT_EQ(0, fn_def->variadic_pos());
}

TEST_F(UDFRegistryTest, test_simple_udaf_register) {
    UDFLibrary library;
    node::NodeManager nm;
    const node::UDAFDefNode* fn_def;

    library.RegisterExprUDF("add").args<AnyArg, AnyArg>(
        [](UDFResolveContext* ctx, ExprNode* x, ExprNode* y) {
            auto res =
                ctx->node_manager()->MakeBinaryExprNode(x, y, node::kFnOpAdd);
            res->SetOutputType(x->GetOutputType());
            return res;
        });

    library.RegisterExprUDF("identity")
        .args<AnyArg>([](UDFResolveContext* ctx, ExprNode* x) { return x; });

    library.RegisterSimpleUDAF("sum")
        .templates<int32_t, int32_t, int32_t>()
        .const_init(0)
        .update("add")
        .merge("add")
        .output("identity")
        .templates<float, double, double>()
        .const_init(0.0)
        .update("add")
        .merge("add")
        .output("identity")
        .finalize();

    fn_def = dynamic_cast<const node::UDAFDefNode*>(
        GetFnDef<codec::ListRef<int32_t>>(&library, "sum", &nm));
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kUDAFDef);

    fn_def = dynamic_cast<const node::UDAFDefNode*>(
        GetFnDef<codec::ListRef<int32_t>>(&library, "sum", &nm));
    ASSERT_TRUE(fn_def != nullptr && fn_def->GetType() == node::kUDAFDef);

    fn_def = dynamic_cast<const node::UDAFDefNode*>(
        GetFnDef<codec::ListRef<StringRef>>(&library, "sum", &nm));
    ASSERT_TRUE(fn_def == nullptr);
}

TEST_F(UDFRegistryTest, test_codegen_udf_register) {
    UDFLibrary library;
    node::NodeManager nm;
    const node::UDFByCodeGenDefNode* fn_def;

    library.RegisterCodeGenUDF("add")
        .allow_window(false)
        .allow_project(true)
        .args<AnyArg, AnyArg>(
            /* infer */ [](UDFResolveContext* ctx, const TypeNode* x,
                           const TypeNode* y) { return x; },
            /* gen */
            [](CodeGenContext* ctx, NativeValue x, NativeValue y,
               NativeValue* out) {
                *out = x;
                return Status::OK();
            });

    fn_def = dynamic_cast<const node::UDFByCodeGenDefNode*>(
        GetFnDef<int32_t, int32_t>(&library, "add", &nm));
    ASSERT_TRUE(fn_def != nullptr &&
                fn_def->GetType() == node::kUDFByCodeGenDef);
}

TEST_F(UDFRegistryTest, test_variadic_codegen_udf_register) {
    UDFLibrary library;
    node::NodeManager nm;
    const node::UDFByCodeGenDefNode* fn_def;

    library.RegisterCodeGenUDF("concat").variadic_args<>(
        /* infer */ [](UDFResolveContext* ctx,
                       const std::vector<const node::TypeNode*>&
                           arg_types) { return arg_types[0]; },
        /* gen */
        [](CodeGenContext* ctx, const std::vector<NativeValue>& args,
           NativeValue* out) {
            *out = args[0];
            return Status::OK();
        });

    fn_def = dynamic_cast<const node::UDFByCodeGenDefNode*>(
        GetFnDef<int32_t, int32_t>(&library, "concat", &nm));
    ASSERT_TRUE(fn_def != nullptr &&
                fn_def->GetType() == node::kUDFByCodeGenDef);
}

}  // namespace udf
}  // namespace fesql

int main(int argc, char** argv) {
    ::testing::GTEST_FLAG(color) = "yes";
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
