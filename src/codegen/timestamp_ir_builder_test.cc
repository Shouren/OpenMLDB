/*-------------------------------------------------------------------------
 * Copyright (C) 2020, 4paradigm
 * timestamp_ir_builder_test.cc
 *
 * Author: chenjing
 * Date: 2020/5/22
 *--------------------------------------------------------------------------
 **/
#include "codegen/timestamp_ir_builder.h"
#include <memory>
#include <utility>
#include "codegen/ir_base_builder.h"
#include "gtest/gtest.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "node/node_manager.h"

using namespace llvm;       // NOLINT
using namespace llvm::orc;  // NOLINT
ExitOnError ExitOnErr;
namespace fesql {
namespace codegen {
class TimestampIRBuilderTest : public ::testing::Test {
 public:
    TimestampIRBuilderTest() {}
    ~TimestampIRBuilderTest() {}
};

TEST_F(TimestampIRBuilderTest, BuildTimestampWithTs_TEST) {
    auto ctx = llvm::make_unique<LLVMContext>();
    auto m = make_unique<Module>("timestamp_test", *ctx);
    TimestampIRBuilder timestamp_builder(m.get());
    Int64IRBuilder int64_builder;
    ::llvm::Type *timestamp_type = timestamp_builder.GetType();
    Function *load_fn = Function::Create(
        FunctionType::get(timestamp_type->getPointerTo(),
                          {int64_builder.GetType(m.get())}, false),
        Function::ExternalLinkage, "build_timestamp", m.get());

    auto iter = load_fn->arg_begin();
    Argument *arg0 = &(*iter);
    iter++;
    BasicBlock *entry_block = BasicBlock::Create(*ctx, "EntryBlock", load_fn);
    IRBuilder<> builder(entry_block);
    ::llvm::Value *ts = arg0;
    ::llvm::Value *output;
    ASSERT_TRUE(timestamp_builder.NewTimestamp(entry_block, ts, &output));
    builder.CreateRet(output);

    m->print(::llvm::errs(), NULL);
    auto J = ExitOnErr(LLJITBuilder().create());
    ExitOnErr(J->addIRModule(ThreadSafeModule(std::move(m), std::move(ctx))));
    auto load_fn_jit = ExitOnErr(J->lookup("build_timestamp"));
    codec::Timestamp *(*decode)(int64_t) =
        (codec::Timestamp * (*)(int64_t)) load_fn_jit.getAddress();

    ASSERT_EQ(static_cast<int64_t>(decode(1590115420000L)->ts_),
              1590115420000L);
    ASSERT_EQ(static_cast<int64_t>(decode(1L)->ts_), 1L);
}

TEST_F(TimestampIRBuilderTest, GetTsTest) {
    auto ctx = llvm::make_unique<LLVMContext>();
    auto m = make_unique<Module>("timestamp_test", *ctx);
    TimestampIRBuilder timestamp_builder(m.get());
    Int64IRBuilder int64_builder;
    Function *load_fn = Function::Create(
        FunctionType::get(int64_builder.GetType(m.get()),
                          {int64_builder.GetType(m.get())}, false),
        Function::ExternalLinkage, "build_timestamp", m.get());
    auto iter = load_fn->arg_begin();
    Argument *arg0 = &(*iter);
    iter++;
    BasicBlock *entry_block = BasicBlock::Create(*ctx, "EntryBlock", load_fn);
    IRBuilder<> builder(entry_block);
    ::llvm::Value *ts = arg0;
    ::llvm::Value *timestamp;
    ASSERT_TRUE(timestamp_builder.NewTimestamp(entry_block, ts, &timestamp));
    ::llvm::Value *output;
    timestamp_builder.GetTs(entry_block, timestamp, &output);
    builder.CreateRet(output);

    m->print(::llvm::errs(), NULL);
    auto J = ExitOnErr(LLJITBuilder().create());
    ExitOnErr(J->addIRModule(ThreadSafeModule(std::move(m), std::move(ctx))));
    auto load_fn_jit = ExitOnErr(J->lookup("build_timestamp"));
    int64_t (*decode)(int64_t) = (int64_t(*)(int64_t))load_fn_jit.getAddress();

    ASSERT_EQ(decode(1590115420000L), 1590115420000L);
    ASSERT_EQ(decode(1L), 1L);
}
TEST_F(TimestampIRBuilderTest, SetTsTest) {
    auto ctx = llvm::make_unique<LLVMContext>();
    auto m = make_unique<Module>("timestamp_test", *ctx);
    TimestampIRBuilder timestamp_builder(m.get());
    Int64IRBuilder int64_builder;
    Function *load_fn = Function::Create(
        FunctionType::get(::llvm::Type::getVoidTy(m->getContext()),
                          {timestamp_builder.GetType()->getPointerTo(),
                           int64_builder.GetType(m.get())},
                          false),
        Function::ExternalLinkage, "build_timestamp", m.get());
    auto iter = load_fn->arg_begin();
    Argument *arg0 = &(*iter);
    iter++;
    Argument *arg1 = &(*iter);
    iter++;
    BasicBlock *entry_block = BasicBlock::Create(*ctx, "EntryBlock", load_fn);
    IRBuilder<> builder(entry_block);
    ::llvm::Value *timestamp = arg0;
    ::llvm::Value *ts = arg1;
    timestamp_builder.SetTs(entry_block, timestamp, ts);
    builder.CreateRetVoid();

    m->print(::llvm::errs(), NULL);
    auto J = ExitOnErr(LLJITBuilder().create());
    ExitOnErr(J->addIRModule(ThreadSafeModule(std::move(m), std::move(ctx))));
    auto load_fn_jit = ExitOnErr(J->lookup("build_timestamp"));
    void (*set_ts)(codec::Timestamp *, int64_t) =
        (void (*)(codec::Timestamp *, int64_t))load_fn_jit.getAddress();

    codec::Timestamp data;
    set_ts(&data, 1590115420000L);
    ASSERT_EQ(data.ts_, 1590115420000L);
    set_ts(&data, 1L);
    ASSERT_EQ(data.ts_, 1L);
}

TEST_F(TimestampIRBuilderTest, TimestampOp) {
    codec::Timestamp t1(1);
    codec::Timestamp t2(10);

    ASSERT_EQ(11L, (t1 + t2).ts_);
    ASSERT_EQ(9L, (t2 - t1).ts_);
    ASSERT_EQ(3, (t2 / 3L).ts_);

    ASSERT_TRUE(t2 >= codec::Timestamp(10));
    ASSERT_TRUE(t2 <= codec::Timestamp(10));

    ASSERT_TRUE(t2 > codec::Timestamp(9));
    ASSERT_TRUE(t2 < codec::Timestamp(11));

    ASSERT_TRUE(t2 == codec::Timestamp(10));
    ASSERT_TRUE(t2 != codec::Timestamp(9));
    if (t2.ts_ > INT32_MAX) {
        codec::Timestamp t3(10);
        t3 += t1;
        ASSERT_EQ(11, t3.ts_);
    }
    {
        codec::Timestamp t3(10);
        t3 -= t1;
        ASSERT_EQ(9, t3.ts_);
    }
}

}  // namespace codegen
}  // namespace fesql

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    return RUN_ALL_TESTS();
}
