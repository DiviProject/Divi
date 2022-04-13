// Copyright (c) 2021 The DIVI developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "defaultValues.h"
#include "primitives/transaction.h"
#include "script/interpreter.h"
#include "script/script.h"
#include "script/scriptandsigflags.h"
#include "script/script_error.h"
#include "script/SignatureCheckers.h"
#include "uint256.h"

#include <boost/test/unit_test.hpp>
#include "test_only.h"

namespace
{

class ScriptCLTVTestFixture
{

protected:

  /** A simple dummy transaction that we use with a TransactionSignatureChecker
   *  during script verification.  It has two inputs (of which we assume
   *  input 0 is the one we are checking), both set to sequence number 1 by
   *  default (which means that CLTV will validate).  */
  CMutableTransaction tx;

  ScriptCLTVTestFixture()
  {
    tx.vin.emplace_back(uint256(), 0, CScript(), 1);
    tx.vin.emplace_back(uint256(), 0, CScript(), 1);
  }

  /** Asserts that a given script is invalid with CLTV enabled in combination
   *  with our dummy tx, but valid without the flag set.  */
  void AssertInvalid(const CScript& script, const ScriptError expectedErr = SCRIPT_ERR_CLTV)
  {
    unsigned flags = SCRIPT_VERIFY_NONE;
    const MutableTransactionSignatureChecker checker(&tx, 0);

    ScriptError err;
    BOOST_CHECK_MESSAGE(VerifyScript(CScript(), {0, script}, flags, checker, &err),
                        "Script failed without CLTV enabled");
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);

    flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
    BOOST_CHECK_MESSAGE(!VerifyScript(CScript(), {0, script}, flags, checker, &err),
                        "Script did not fail with CLTV enabled");
    BOOST_CHECK_EQUAL(err, expectedErr);
  }

  /** Asserts that a given script is valid with our dummy tx (which should be the
   *  case with and without CLTV enabled).  */
  void AssertValid(const CScript& script)
  {
    unsigned flags = SCRIPT_VERIFY_NONE;
    const MutableTransactionSignatureChecker checker(&tx, 0);

    ScriptError err;
    BOOST_CHECK_MESSAGE(VerifyScript(CScript(), {0, script}, flags, checker, &err),
                        "Valid script failed without CLTV enabled");
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);

    flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
    BOOST_CHECK_MESSAGE(VerifyScript(CScript(), {0, script}, flags, checker, &err),
                        "Valid script failed with CLTV enabled");
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
  }

};

BOOST_FIXTURE_TEST_SUITE(script_CLTV_tests, ScriptCLTVTestFixture)

BOOST_AUTO_TEST_CASE(failsWithEmptyStack)
{
  AssertInvalid(CScript() << OP_CHECKLOCKTIMEVERIFY << OP_TRUE,
                SCRIPT_ERR_INVALID_STACK_OPERATION);
}

BOOST_AUTO_TEST_CASE(failsIfArgumentTooLarge)
{
  AssertInvalid(CScript() << CScriptNum(1LL << 50) << OP_CHECKLOCKTIMEVERIFY << OP_TRUE,
                SCRIPT_ERR_UNKNOWN_ERROR);
}

BOOST_AUTO_TEST_CASE(failsIfArgumentIsNegative)
{
  tx.nLockTime = 0;
  AssertInvalid(CScript() << CScriptNum(-1) << OP_CHECKLOCKTIMEVERIFY << OP_TRUE);
}

BOOST_AUTO_TEST_CASE(failsIfLockTypeMismatch)
{
  /* Note that this case is kind of redundant, as it would fail anyway in the
     straight comparison between the lock times.  The second case below would
     succeed without a special check, though.  */
  tx.nLockTime = LOCKTIME_THRESHOLD - 1;
  AssertInvalid(CScript() << CScriptNum(LOCKTIME_THRESHOLD) << OP_CHECKLOCKTIMEVERIFY << OP_TRUE);

  tx.nLockTime = LOCKTIME_THRESHOLD;
  AssertInvalid(CScript() << CScriptNum(LOCKTIME_THRESHOLD - 1) << OP_CHECKLOCKTIMEVERIFY << OP_TRUE);
}

BOOST_AUTO_TEST_CASE(failsIfInputIsFinal)
{
  tx.vin[0].nSequence = 0xFFFFFFFF;
  tx.nLockTime = 0;
  AssertInvalid(CScript() << CScriptNum(0) << OP_CHECKLOCKTIMEVERIFY << OP_TRUE);
}

BOOST_AUTO_TEST_CASE(verifiesLockByTimeCorrectly)
{
  tx.nLockTime = LOCKTIME_THRESHOLD + 10;
  AssertInvalid(CScript() << CScriptNum(LOCKTIME_THRESHOLD + 11) << OP_CHECKLOCKTIMEVERIFY << OP_TRUE);
  AssertValid(CScript() << CScriptNum(LOCKTIME_THRESHOLD + 10) << OP_CHECKLOCKTIMEVERIFY << OP_TRUE);
  AssertValid(CScript() << CScriptNum(LOCKTIME_THRESHOLD) << OP_CHECKLOCKTIMEVERIFY << OP_TRUE);
}

BOOST_AUTO_TEST_CASE(verifiesLockByHeightCorrectly)
{
  tx.nLockTime = 10;
  AssertInvalid(CScript() << CScriptNum(11) << OP_CHECKLOCKTIMEVERIFY << OP_TRUE);
  AssertValid(CScript() << CScriptNum(10) << OP_CHECKLOCKTIMEVERIFY << OP_TRUE);
  AssertValid(CScript() << CScriptNum(0) << OP_CHECKLOCKTIMEVERIFY << OP_TRUE);
}

BOOST_AUTO_TEST_CASE(accepts32BitLockTimes)
{
  tx.nLockTime = 0xFFFFFFFF;
  AssertValid(CScript() << CScriptNum(0xFFFFFFFF) << OP_CHECKLOCKTIMEVERIFY << OP_TRUE);
}

BOOST_AUTO_TEST_CASE(acceptsOtherInputsBeingFinal)
{
  tx.vin[1].nSequence = 0xFFFFFFFF;
  tx.nLockTime = 10;
  AssertValid(CScript() << CScriptNum(10) << OP_CHECKLOCKTIMEVERIFY << OP_TRUE);
}

BOOST_AUTO_TEST_SUITE_END()

} // anonymous namespace
