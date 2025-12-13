// Copyright (c) 2025 The Ycash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "amount.h"
#include "consensus/upgrades.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "experimental_features.h"
#include "init.h"
#include "key_io.h"
#include "keystore.h"
#include "main.h"
#include "net.h"
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "script/atomicswap.h"
#include "script/interpreter.h"
#include "script/script.h"
#include "script/sign.h"
#include "script/standard.h"
#include "sync.h"
#include "uint256.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "utiltime.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <stdint.h>
#include <univalue.h>

using namespace std;

// Function declaration for function implemented in wallet/rpcwallet.cpp
bool EnsureWalletIsAvailable(bool avoidException);

/**
 * initiateswap "fundingaddress" "recipientaddress" amount locktime ( secret )
 *
 * Initiate an atomic swap by creating an HTLC contract.
 * This locks funds that can be claimed by the recipient if they reveal the secret,
 * or refunded by the initiator after the locktime expires.
 *
 * Arguments:
 * 1. fundingaddress   (string, required) Existing wallet address with sufficient balance to fund the swap
 * 2. recipientaddress (string, required) The Ycash address of the recipient
 * 3. amount           (numeric, required) The amount in YEC to lock
 * 4. locktime         (numeric, required) The absolute locktime (block height or Unix timestamp)
 * 5. secret           (string, optional) The 32-byte secret in hex (generated if not provided)
 *
 * Result:
 * {
 *   "contract": "hex",           (string) The HTLC contract (redeem script) in hex
 *   "contractP2SH": "address",   (string) The P2SH address for the contract
 *   "contractTxid": "hex",       (string) The transaction ID of the contract
 *   "contractVout": n,           (numeric) The output index
 *   "secret": "hex",             (string) The secret preimage in hex
 *   "secretHash": "hex",         (string) The HASH160 of the secret
 *   "refundLocktime": n          (numeric) The locktime for refunds
 * }
 */
UniValue initiateswap(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 4 || params.size() > 5)
        throw runtime_error(
            "initiateswap \"fundingaddress\" \"recipientaddress\" amount locktime ( \"secret\" )\n"
            "\nInitiate an atomic swap by creating an HTLC contract.\n"
            "\nArguments:\n"
            "1. \"fundingaddress\"    (string, required) Existing wallet address with sufficient balance\n"
            "2. \"recipientaddress\"  (string, required) The Ycash address of the recipient\n"
            "3. amount              (numeric, required) The amount in YEC to lock\n"
            "4. locktime            (numeric, required) Absolute locktime (block height < 500000000 or Unix timestamp >= 500000000)\n"
            "5. \"secret\"            (string, optional) 32-byte secret in hex (generated if not provided)\n"
            "\nResult:\n"
            "{\n"
            "  \"contract\": \"hex\",           (string) The HTLC contract (redeem script) in hex\n"
            "  \"contractP2SH\": \"address\",   (string) The P2SH address for the contract\n"
            "  \"contractTxid\": \"hex\",       (string) The transaction ID of the contract\n"
            "  \"contractVout\": n,           (numeric) The output index\n"
            "  \"secret\": \"hex\",             (string) The secret preimage in hex\n"
            "  \"secretHash\": \"hex\",         (string) The HASH160 of the secret\n"
            "  \"refundLocktime\": n          (numeric) The locktime for refunds\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("initiateswap", "\"s1FundingAddressHere\" \"s1cXmTZneHwrWZ2jzzUcvpjHgLC6njnnnnn\" 1.0 550000")
            + HelpExampleRpc("initiateswap", "\"s1FundingAddressHere\", \"s1cXmTZneHwrWZ2jzzUcvpjHgLC6njnnnnn\", 1.0, 550000")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    KeyIO keyIO(Params());

    // Parse funding address (first parameter)
    CTxDestination fundingDest = keyIO.DecodeDestination(params[0].get_str());
    if (!IsValidDestination(fundingDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid funding address");
    }
    if (!IsKeyDestination(fundingDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Funding address must be a P2PKH address");
    }

    CKeyID fundingKeyID = std::get<CKeyID>(fundingDest);

    // Validate funding address is in wallet
    if (IsMine(*pwalletMain, fundingKeyID) == ISMINE_NO) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Funding address does not belong to this wallet");
    }

    // Parse recipient address
    CTxDestination recipientDest = keyIO.DecodeDestination(params[1].get_str());
    if (!IsValidDestination(recipientDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid recipient address");
    }
    if (!IsKeyDestination(recipientDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Recipient address must be a P2PKH address");
    }

    // Get recipient key ID
    CKeyID recipientKeyID = std::get<CKeyID>(recipientDest);

    // Parse amount
    CAmount nAmount = AmountFromValue(params[2]);
    if (nAmount <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Amount must be positive");
    }

    // Validate funding address has sufficient balance
    std::map<CTxDestination, CAmount> addressBalances = pwalletMain->GetAddressBalances();
    CAmount fundingBalance = 0;
    CTxDestination fundingDestCheck(fundingKeyID);
    if (addressBalances.count(fundingDestCheck)) {
        fundingBalance = addressBalances[fundingDestCheck];
    }
    if (fundingBalance < nAmount) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Funding address has insufficient balance. Required: %s, Available: %s",
                      FormatMoney(nAmount), FormatMoney(fundingBalance)));
    }

    // Parse locktime
    int64_t lockTime = params[3].get_int64();
    if (lockTime <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Locktime must be positive");
    }

    // Generate or parse secret
    vector<unsigned char> secret;
    if (params.size() > 4 && !params[4].isNull()) {
        string secretHex = params[4].get_str();
        if (!IsHex(secretHex)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Secret must be hex encoded");
        }
        secret = ParseHex(secretHex);
        if (secret.size() != 32) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Secret must be 32 bytes");
        }
    } else {
        secret = GenerateAtomicSwapSecret();
    }

    // Calculate secret hash
    uint160 secretHash = Hash160(secret);

    // Use fundingKeyID as initiatorKeyID
    CKeyID initiatorKeyID = fundingKeyID;

    // Build contract
    AtomicSwapContract contract(secretHash, recipientKeyID, initiatorKeyID, lockTime);

    // Validate contract
    string error;
    if (!ValidateAtomicSwapContract(contract, chainActive.Height(), error)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, error);
    }

    // Build HTLC script
    CScript redeemScript = BuildAtomicSwapScript(contract);

    // Create P2SH address
    CScriptID scriptID(redeemScript);
    CTxDestination contractDest(scriptID);
    string contractAddress = keyIO.EncodeDestination(contractDest);

    // Create transaction to fund the contract
    CMutableTransaction mtx;
    mtx.nVersion = SAPLING_TX_VERSION;
    mtx.fOverwintered = true;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
    mtx.nExpiryHeight = chainActive.Height() + 20; // 20 blocks

    // Add output to P2SH address
    CScript scriptPubKey = GetScriptForDestination(scriptID);
    mtx.vout.push_back(CTxOut(nAmount, scriptPubKey));

    // Fund the transaction
    CAmount nFeeRequired;
    string strError;
    vector<CRecipient> vecSend;
    int nChangePosRet = -1;

    CRecipient recipient = {scriptPubKey, nAmount, false};
    vecSend.push_back(recipient);

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    if (!pwalletMain->CreateTransaction(vecSend, wtx, reservekey, nFeeRequired, nChangePosRet, strError)) {
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    // Send the transaction
    if (!pwalletMain->CommitTransaction(wtx, reservekey)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent.");
    }

    // Find the output index
    uint32_t vout = 0;
    for (uint32_t i = 0; i < wtx.vout.size(); i++) {
        if (wtx.vout[i].scriptPubKey == scriptPubKey && wtx.vout[i].nValue == nAmount) {
            vout = i;
            break;
        }
    }

    // Persist swap data to wallet
    CAtomicSwapInfo swapInfo(
        wtx.GetHash(),
        vout,
        redeemScript,
        nAmount,
        contract,
        ROLE_INITIATOR
    );
    swapInfo.secret = secret;
    swapInfo.secretKnown = true;

    if (!pwalletMain->AddAtomicSwap(swapInfo)) {
        LogPrintf("Warning: Failed to persist atomic swap to wallet: %s\n", swapInfo.GetSwapId());
    }

    // Return result
    UniValue result(UniValue::VOBJ);
    result.pushKV("contract", HexStr(redeemScript.begin(), redeemScript.end()));
    result.pushKV("contractP2SH", contractAddress);
    result.pushKV("contractTxid", wtx.GetHash().GetHex());
    result.pushKV("contractVout", (int)vout);
    result.pushKV("secret", HexStr(secret.begin(), secret.end()));
    result.pushKV("secretHash", secretHash.GetHex());
    result.pushKV("refundLocktime", lockTime);

    // Add human-readable locktime if it's a Unix timestamp (>= 500000000)
    if (lockTime >= 500000000) {
        result.pushKV("refundLocktimeFormatted", DateTimeStrFormat("%Y-%m-%d %H:%M:%S UTC", lockTime));
    }

    return result;
}

/**
 * initiateswapfromhash "fundingaddress" "recipientaddress" amount locktime "secrethash"
 *
 * Initiate an atomic swap using a known secret hash (when you don't know the secret).
 * Use this when participating in a cross-chain swap where the counterparty generated the secret.
 *
 * Arguments:
 * 1. fundingaddress    (string, required) Existing wallet address with sufficient balance
 * 2. recipientaddress  (string, required) The Ycash address of the recipient
 * 3. amount           (numeric, required) The amount in YEC to lock
 * 4. locktime         (numeric, required) Absolute locktime (block height < 500000000 or Unix timestamp >= 500000000)
 * 5. secrethash       (string, required) 20-byte secret hash (HASH160) in hex
 *
 * Result:
 * {
 *   "contract": "hex",           (string) The HTLC contract (redeem script) in hex
 *   "contractP2SH": "address",   (string) The P2SH address for the contract
 *   "contractTxid": "hex",       (string) The transaction ID of the contract
 *   "contractVout": n,           (numeric) The output index
 *   "secretHash": "hex",         (string) The HASH160 of the secret
 *   "refundLocktime": n          (numeric) The locktime for refunds
 * }
 */
UniValue initiateswapfromhash(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 5)
        throw runtime_error(
            "initiateswapfromhash \"fundingaddress\" \"recipientaddress\" amount locktime \"secrethash\"\n"
            "\nInitiate an atomic swap using a known secret hash (when you don't know the secret).\n"
            "Use this when participating in a cross-chain swap where the counterparty generated the secret.\n"
            "\nArguments:\n"
            "1. \"fundingaddress\"    (string, required) Existing wallet address with sufficient balance\n"
            "2. \"recipientaddress\"  (string, required) The Ycash address of the recipient\n"
            "3. amount              (numeric, required) The amount in YEC to lock\n"
            "4. locktime            (numeric, required) Absolute locktime (block height < 500000000 or Unix timestamp >= 500000000)\n"
            "5. \"secrethash\"        (string, required) 20-byte secret hash (HASH160) in hex\n"
            "\nResult:\n"
            "{\n"
            "  \"contract\": \"hex\",           (string) The HTLC contract (redeem script) in hex\n"
            "  \"contractP2SH\": \"address\",   (string) The P2SH address for the contract\n"
            "  \"contractTxid\": \"hex\",       (string) The transaction ID of the contract\n"
            "  \"contractVout\": n,           (numeric) The output index\n"
            "  \"secretHash\": \"hex\",         (string) The HASH160 of the secret\n"
            "  \"refundLocktime\": n          (numeric) The locktime for refunds\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("initiateswapfromhash", "\"s1FundingAddressHere\" \"s1cXmTZneHwrWZ2jzzUcvpjHgLC6njnnnnn\" 1.0 550000 \"a1b2c3d4e5f6...\"")
            + HelpExampleRpc("initiateswapfromhash", "\"s1FundingAddressHere\", \"s1cXmTZneHwrWZ2jzzUcvpjHgLC6njnnnnn\", 1.0, 550000, \"a1b2c3d4e5f6...\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    KeyIO keyIO(Params());

    // Parse funding address (first parameter)
    CTxDestination fundingDest = keyIO.DecodeDestination(params[0].get_str());
    if (!IsValidDestination(fundingDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid funding address");
    }
    if (!IsKeyDestination(fundingDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Funding address must be a P2PKH address");
    }

    CKeyID fundingKeyID = std::get<CKeyID>(fundingDest);

    // Validate funding address is in wallet
    if (IsMine(*pwalletMain, fundingKeyID) == ISMINE_NO) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Funding address does not belong to this wallet");
    }

    // Parse recipient address
    CTxDestination recipientDest = keyIO.DecodeDestination(params[1].get_str());
    if (!IsValidDestination(recipientDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid recipient address");
    }
    if (!IsKeyDestination(recipientDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Recipient address must be a P2PKH address");
    }

    // Get recipient key ID
    CKeyID recipientKeyID = std::get<CKeyID>(recipientDest);

    // Parse amount
    CAmount nAmount = AmountFromValue(params[2]);
    if (nAmount <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Amount must be positive");
    }

    // Validate funding address has sufficient balance
    std::map<CTxDestination, CAmount> addressBalances = pwalletMain->GetAddressBalances();
    CAmount fundingBalance = 0;
    CTxDestination fundingDestCheck(fundingKeyID);
    if (addressBalances.count(fundingDestCheck)) {
        fundingBalance = addressBalances[fundingDestCheck];
    }
    if (fundingBalance < nAmount) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Funding address has insufficient balance. Required: %s, Available: %s",
                      FormatMoney(nAmount), FormatMoney(fundingBalance)));
    }

    // Parse locktime
    int64_t lockTime = params[3].get_int64();
    if (lockTime <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Locktime must be positive");
    }

    // Parse secret hash (20 bytes)
    string secretHashHex = params[4].get_str();
    if (!IsHex(secretHashHex)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Secret hash must be hex encoded");
    }
    if (secretHashHex.size() != 40) {  // 20 bytes = 40 hex chars
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Secret hash must be 20 bytes (40 hex characters)");
    }

    // Use SetHex to properly handle endianness (reverses for internal storage)
    uint160 secretHash;
    secretHash.SetHex(secretHashHex);

    // Use fundingKeyID as initiatorKeyID
    CKeyID initiatorKeyID = fundingKeyID;

    // Build contract
    AtomicSwapContract contract(secretHash, recipientKeyID, initiatorKeyID, lockTime);

    // Validate contract
    string error;
    if (!ValidateAtomicSwapContract(contract, chainActive.Height(), error)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, error);
    }

    // Build HTLC script
    CScript redeemScript = BuildAtomicSwapScript(contract);

    // Create P2SH address
    CScriptID scriptID(redeemScript);
    CTxDestination contractDest(scriptID);
    string contractAddress = keyIO.EncodeDestination(contractDest);

    // Create transaction to fund the contract
    CMutableTransaction mtx;
    mtx.nVersion = 4;  // Sapling version
    mtx.fOverwintered = true;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
    mtx.nExpiryHeight = chainActive.Height() + 20;

    // Add output to P2SH address
    CScript scriptPubKey = GetScriptForDestination(scriptID);
    mtx.vout.push_back(CTxOut(nAmount, scriptPubKey));

    // Fund the transaction
    CAmount nFeeRequired;
    string strError;
    vector<CRecipient> vecSend;
    int nChangePosRet = -1;

    CRecipient recipient = {scriptPubKey, nAmount, false};
    vecSend.push_back(recipient);

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    if (!pwalletMain->CreateTransaction(vecSend, wtx, reservekey, nFeeRequired, nChangePosRet, strError)) {
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    // Send the transaction
    if (!pwalletMain->CommitTransaction(wtx, reservekey)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent.");
    }

    // Find the output index
    uint32_t vout = 0;
    for (uint32_t i = 0; i < wtx.vout.size(); i++) {
        if (wtx.vout[i].scriptPubKey == scriptPubKey && wtx.vout[i].nValue == nAmount) {
            vout = i;
            break;
        }
    }

    // Persist swap data to wallet (secret unknown)
    CAtomicSwapInfo swapInfo(
        wtx.GetHash(),
        vout,
        redeemScript,
        nAmount,
        contract,
        ROLE_INITIATOR
    );
    swapInfo.secretKnown = false;  // We don't know the secret!

    if (!pwalletMain->AddAtomicSwap(swapInfo)) {
        LogPrintf("Warning: Failed to persist atomic swap to wallet: %s\n", swapInfo.GetSwapId());
    }

    // Return result (no secret field since we don't know it)
    UniValue result(UniValue::VOBJ);
    result.pushKV("contract", HexStr(redeemScript.begin(), redeemScript.end()));
    result.pushKV("contractP2SH", contractAddress);
    result.pushKV("contractTxid", wtx.GetHash().GetHex());
    result.pushKV("contractVout", (int)vout);
    result.pushKV("secretHash", secretHash.GetHex());
    result.pushKV("refundLocktime", lockTime);

    // Add human-readable locktime if it's a Unix timestamp (>= 500000000)
    if (lockTime >= 500000000) {
        result.pushKV("refundLocktimeFormatted", DateTimeStrFormat("%Y-%m-%d %H:%M:%S UTC", lockTime));
    }

    return result;
}

/**
 * auditswap "contract" "contracttxid" vout
 *
 * Audit an atomic swap contract to verify its parameters.
 *
 * Arguments:
 * 1. contract        (string, required) The HTLC contract (redeem script) in hex
 * 2. contracttxid    (string, required) The transaction ID of the contract
 * 3. vout            (numeric, required) The output index
 *
 * Result:
 * {
 *   "contractP2SH": "address",       (string) The P2SH address for the contract
 *   "contractValue": n,              (numeric) The value locked in the contract
 *   "recipientAddress": "address",   (string) The address that can claim
 *   "initiatorAddress": "address",   (string) The address that can refund
 *   "secretHash": "hex",             (string) The HASH160 of the secret
 *   "refundLocktime": n,             (numeric) The locktime for refunds
 *   "locktimeReached": true|false    (boolean) Whether the locktime has been reached
 * }
 */
UniValue auditswap(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw runtime_error(
            "auditswap \"contract\" \"contracttxid\" vout\n"
            "\nAudit an atomic swap contract to verify its parameters.\n"
            "\nArguments:\n"
            "1. \"contract\"        (string, required) The HTLC contract (redeem script) in hex\n"
            "2. \"contracttxid\"    (string, required) The transaction ID of the contract\n"
            "3. vout              (numeric, required) The output index\n"
            "\nResult:\n"
            "{\n"
            "  \"contractP2SH\": \"address\",       (string) The P2SH address for the contract\n"
            "  \"contractValue\": n,              (numeric) The value locked in the contract\n"
            "  \"recipientAddress\": \"address\",   (string) The address that can claim\n"
            "  \"initiatorAddress\": \"address\",   (string) The address that can refund\n"
            "  \"secretHash\": \"hex\",             (string) The HASH160 of the secret\n"
            "  \"refundLocktime\": n,             (numeric) The locktime for refunds\n"
            "  \"locktimeReached\": true|false    (boolean) Whether the locktime has been reached\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("auditswap", "\"6382012088a820...\" \"abc123...\" 0")
            + HelpExampleRpc("auditswap", "\"6382012088a820...\", \"abc123...\", 0")
        );

    LOCK(cs_main);

    // Parse contract (redeem script)
    string contractHex = params[0].get_str();
    if (!IsHex(contractHex)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Contract must be hex encoded");
    }
    vector<unsigned char> contractData = ParseHex(contractHex);
    CScript redeemScript(contractData.begin(), contractData.end());

    // Extract contract info
    AtomicSwapContract contract;
    if (!ExtractAtomicSwapContract(redeemScript, contract)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid atomic swap contract");
    }

    // Parse transaction ID
    uint256 txid;
    txid.SetHex(params[1].get_str());

    // Parse vout
    uint32_t vout = params[2].get_int();

    // Get the transaction
    CTransaction tx;
    uint256 blockHash;
    if (!GetTransaction(txid, tx, Params().GetConsensus(), blockHash, true)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not found");
    }

    // Verify vout is valid
    if (vout >= tx.vout.size()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Output index out of range");
    }

    // Verify the output
    if (!VerifyAtomicSwapOutput(tx.vout[vout], redeemScript)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Output does not match contract");
    }

    // Get contract value
    CAmount contractValue = tx.vout[vout].nValue;

    // Calculate P2SH address
    KeyIO keyIO(Params());
    CScriptID scriptID(redeemScript);
    CTxDestination contractDest(scriptID);
    string contractAddress = keyIO.EncodeDestination(contractDest);

    // Get recipient and initiator addresses
    CKeyID recipientKeyID = contract.recipientPubKeyHash;
    CKeyID initiatorKeyID = contract.initiatorPubKeyHash;

    // Encode addresses as P2PKH destinations
    CTxDestination recipientDest(recipientKeyID);
    CTxDestination initiatorDest(initiatorKeyID);
    string recipientAddress = keyIO.EncodeDestination(recipientDest);
    string initiatorAddress = keyIO.EncodeDestination(initiatorDest);

    // Check if locktime has been reached
    bool locktimeReached;
    if (contract.lockTime < LOCKTIME_THRESHOLD) {
        // Block height based
        locktimeReached = chainActive.Height() >= contract.lockTime;
    } else {
        // Timestamp based
        locktimeReached = GetTime() >= contract.lockTime;
    }

    // Return result
    UniValue result(UniValue::VOBJ);
    result.pushKV("contractP2SH", contractAddress);
    result.pushKV("contractValue", ValueFromAmount(contractValue));
    result.pushKV("recipientAddress", recipientAddress);
    result.pushKV("initiatorAddress", initiatorAddress);
    result.pushKV("secretHash", contract.secretHash.GetHex());
    result.pushKV("refundLocktime", contract.lockTime);

    // Add human-readable locktime if it's a Unix timestamp (>= 500000000)
    if (contract.lockTime >= 500000000) {
        result.pushKV("refundLocktimeFormatted", DateTimeStrFormat("%Y-%m-%d %H:%M:%S UTC", contract.lockTime));
    }

    result.pushKV("locktimeReached", locktimeReached);

    return result;
}

/**
 * claimswap "contract" "contracttxid" vout "secret" ( "recipientaddress" )
 *
 * Claim funds from an atomic swap by revealing the secret.
 *
 * Arguments:
 * 1. contract            (string, required) The HTLC contract (redeem script) in hex
 * 2. contracttxid        (string, required) The transaction ID of the contract
 * 3. vout                (numeric, required) The output index
 * 4. secret              (string, required) The 32-byte secret in hex
 * 5. recipientaddress    (string, optional) The address to send claimed funds to
 *
 * Result:
 * {
 *   "txid": "hex",     (string) The claim transaction ID
 *   "hex": "hex"       (string) The raw claim transaction in hex
 * }
 */
UniValue claimswap(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 4 || params.size() > 5)
        throw runtime_error(
            "claimswap \"contract\" \"contracttxid\" vout \"secret\" ( \"recipientaddress\" )\n"
            "\nClaim funds from an atomic swap by revealing the secret.\n"
            "\nArguments:\n"
            "1. \"contract\"            (string, required) The HTLC contract (redeem script) in hex\n"
            "2. \"contracttxid\"        (string, required) The transaction ID of the contract\n"
            "3. vout                  (numeric, required) The output index\n"
            "4. \"secret\"              (string, required) The 32-byte secret in hex\n"
            "5. \"recipientaddress\"    (string, optional) The address to send claimed funds to\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"hex\",     (string) The claim transaction ID\n"
            "  \"hex\": \"hex\"       (string) The raw claim transaction in hex\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("claimswap", "\"6382012088a820...\" \"abc123...\" 0 \"def456...\"")
            + HelpExampleRpc("claimswap", "\"6382012088a820...\", \"abc123...\", 0, \"def456...\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Parse contract (redeem script)
    string contractHex = params[0].get_str();
    if (!IsHex(contractHex)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Contract must be hex encoded");
    }
    vector<unsigned char> contractData = ParseHex(contractHex);
    CScript redeemScript(contractData.begin(), contractData.end());

    // Extract contract info
    AtomicSwapContract contract;
    if (!ExtractAtomicSwapContract(redeemScript, contract)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid atomic swap contract");
    }

    // Parse transaction ID
    uint256 txid;
    txid.SetHex(params[1].get_str());

    // Parse vout
    uint32_t vout = params[2].get_int();

    // Parse secret
    string secretHex = params[3].get_str();
    if (!IsHex(secretHex)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Secret must be hex encoded");
    }
    vector<unsigned char> secret = ParseHex(secretHex);
    if (secret.size() != 32) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Secret must be 32 bytes");
    }

    // Verify secret matches hash
    uint160 providedSecretHash = Hash160(secret);

    if (providedSecretHash != contract.secretHash) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Secret does not match contract hash");
    }

    // Get recipient private key
    CKeyID recipientKeyID = contract.recipientPubKeyHash;
    CKey recipientKey;
    if (!pwalletMain->GetKey(recipientKeyID, recipientKey)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Recipient private key not found in wallet");
    }

    // Get the contract transaction
    CTransaction contractTx;
    uint256 blockHash;
    if (!GetTransaction(txid, contractTx, Params().GetConsensus(), blockHash, true)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Contract transaction not found");
    }

    // Verify vout is valid
    if (vout >= contractTx.vout.size()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Output index out of range");
    }

    // Verify the output
    if (!VerifyAtomicSwapOutput(contractTx.vout[vout], redeemScript)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Output does not match contract");
    }

    // Determine destination for claimed funds
    CTxDestination claimDest;
    KeyIO keyIO(Params());
    if (params.size() > 4 && !params[4].isNull()) {
        claimDest = keyIO.DecodeDestination(params[4].get_str());
        if (!IsValidDestination(claimDest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid recipient address");
        }
    } else {
        // Use recipient's address by default
        claimDest = recipientKeyID;
    }

    // Create claim transaction
    CMutableTransaction mtx;
    mtx.nVersion = SAPLING_TX_VERSION;
    mtx.fOverwintered = true;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
    mtx.nExpiryHeight = chainActive.Height() + 20;

    // Add input spending the contract
    mtx.vin.push_back(CTxIn(COutPoint(txid, vout), CScript(), std::numeric_limits<unsigned int>::max()));

    // Calculate output amount (input amount minus fee)
    CAmount inputAmount = contractTx.vout[vout].nValue;
    CAmount fee = 10000; // 0.0001 YEC fee
    CAmount outputAmount = inputAmount - fee;

    if (outputAmount <= 0) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Contract value too small to pay fee");
    }

    // Add output
    CScript outputScript = GetScriptForDestination(claimDest);
    mtx.vout.push_back(CTxOut(outputAmount, outputScript));

    // Sign the transaction
    CTransaction txConst(mtx);

    // Get consensus branch ID for current height
    auto consensusBranchId = CurrentEpochBranchId(chainActive.Height(), Params().GetConsensus());

    uint256 hash = SignatureHash(redeemScript, txConst, 0, SIGHASH_ALL, inputAmount, consensusBranchId);

    vector<unsigned char> vchSig;
    if (!recipientKey.Sign(hash, vchSig)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign transaction");
    }
    vchSig.push_back((unsigned char)SIGHASH_ALL);

    // Get the public key from the private key
    CPubKey recipientPubKey = recipientKey.GetPubKey();
    vector<unsigned char> vchPubKey(recipientPubKey.begin(), recipientPubKey.end());

    // Build scriptSig for claim
    mtx.vin[0].scriptSig = BuildAtomicSwapClaimScript(secret, vchSig, vchPubKey, redeemScript);

    // Broadcast transaction
    CTransaction claimTx(mtx);

    CValidationState state;
    bool fMissingInputs;
    if (!AcceptToMemoryPool(Params(), mempool, state, claimTx, false, &fMissingInputs, true)) {
        if (state.IsInvalid()) {
            throw JSONRPCError(RPC_TRANSACTION_REJECTED,
                strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
        } else {
            if (fMissingInputs) {
                throw JSONRPCError(RPC_TRANSACTION_ERROR, "Missing inputs");
            }
            throw JSONRPCError(RPC_TRANSACTION_ERROR, state.GetRejectReason());
        }
    }

    RelayTransaction(claimTx);

    // Update swap status in database
    std::string swapId = txid.GetHex() + ":" + std::to_string(vout);
    CAtomicSwapInfo swapInfo;
    if (pwalletMain->GetAtomicSwap(swapId, swapInfo)) {
        // Update existing swap record
        swapInfo.status = SWAP_CLAIMED;
        swapInfo.completedTime = GetTime();
        swapInfo.spendTxid = claimTx.GetHash();
        if (!swapInfo.secretKnown) {
            swapInfo.secret = secret;
            swapInfo.secretKnown = true;
        }
        if (!pwalletMain->UpdateAtomicSwap(swapInfo)) {
            LogPrintf("Warning: Failed to update atomic swap status: %s\n", swapId);
        }
    } else {
        // Create new swap record (participant claiming)
        // Only store if we own at least one of the addresses involved
        CKeyID recipientKeyID = contract.recipientPubKeyHash;
        CKeyID initiatorKeyID = contract.initiatorPubKeyHash;

        bool isOurs = (IsMine(*pwalletMain, recipientKeyID) != ISMINE_NO) ||
                      (IsMine(*pwalletMain, initiatorKeyID) != ISMINE_NO);

        if (isOurs) {
            CAtomicSwapInfo newSwap(
                txid,
                vout,
                redeemScript,
                inputAmount,
                contract,
                ROLE_PARTICIPANT
            );
            newSwap.status = SWAP_CLAIMED;
            newSwap.completedTime = GetTime();
            newSwap.spendTxid = claimTx.GetHash();
            newSwap.secret = secret;
            newSwap.secretKnown = true;
            if (!pwalletMain->AddAtomicSwap(newSwap)) {
                LogPrintf("Warning: Failed to persist atomic swap to wallet: %s\n", swapId);
            }
        }
    }

    // Return result
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", claimTx.GetHash().GetHex());
    result.pushKV("hex", EncodeHexTx(claimTx));

    return result;
}

/**
 * refundswap "contract" "contracttxid" vout ( "refundaddress" )
 *
 * Refund an atomic swap after the locktime has expired.
 *
 * Arguments:
 * 1. contract         (string, required) The HTLC contract (redeem script) in hex
 * 2. contracttxid     (string, required) The transaction ID of the contract
 * 3. vout             (numeric, required) The output index
 * 4. refundaddress    (string, optional) The address to send refunded funds to
 *
 * Result:
 * {
 *   "txid": "hex",     (string) The refund transaction ID
 *   "hex": "hex"       (string) The raw refund transaction in hex
 * }
 */
UniValue refundswap(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 3 || params.size() > 4)
        throw runtime_error(
            "refundswap \"contract\" \"contracttxid\" vout ( \"refundaddress\" )\n"
            "\nRefund an atomic swap after the locktime has expired.\n"
            "\nArguments:\n"
            "1. \"contract\"         (string, required) The HTLC contract (redeem script) in hex\n"
            "2. \"contracttxid\"     (string, required) The transaction ID of the contract\n"
            "3. vout               (numeric, required) The output index\n"
            "4. \"refundaddress\"    (string, optional) The address to send refunded funds to\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"hex\",     (string) The refund transaction ID\n"
            "  \"hex\": \"hex\"       (string) The raw refund transaction in hex\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("refundswap", "\"6382012088a820...\" \"abc123...\" 0")
            + HelpExampleRpc("refundswap", "\"6382012088a820...\", \"abc123...\", 0")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Parse contract (redeem script)
    string contractHex = params[0].get_str();
    if (!IsHex(contractHex)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Contract must be hex encoded");
    }
    vector<unsigned char> contractData = ParseHex(contractHex);
    CScript redeemScript(contractData.begin(), contractData.end());

    // Extract contract info
    AtomicSwapContract contract;
    if (!ExtractAtomicSwapContract(redeemScript, contract)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid atomic swap contract");
    }

    // Check if locktime has been reached
    bool locktimeReached;
    if (contract.lockTime < LOCKTIME_THRESHOLD) {
        locktimeReached = chainActive.Height() >= contract.lockTime;
    } else {
        locktimeReached = GetTime() >= contract.lockTime;
    }

    if (!locktimeReached) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            "Locktime has not been reached yet. Current: " +
            (contract.lockTime < LOCKTIME_THRESHOLD ?
                std::to_string(chainActive.Height()) : std::to_string(GetTime())) +
            ", Required: " + std::to_string(contract.lockTime));
    }

    // Parse transaction ID
    uint256 txid;
    txid.SetHex(params[1].get_str());

    // Parse vout
    uint32_t vout = params[2].get_int();

    // Get initiator private key and public key
    CKeyID initiatorKeyID = contract.initiatorPubKeyHash;
    CKey initiatorKey;
    if (!pwalletMain->GetKey(initiatorKeyID, initiatorKey)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Initiator private key not found in wallet");
    }
    CPubKey initiatorPubKey = initiatorKey.GetPubKey();

    // Get the contract transaction
    CTransaction contractTx;
    uint256 blockHash;
    if (!GetTransaction(txid, contractTx, Params().GetConsensus(), blockHash, true)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Contract transaction not found");
    }

    // Verify vout is valid
    if (vout >= contractTx.vout.size()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Output index out of range");
    }

    // Verify the output
    if (!VerifyAtomicSwapOutput(contractTx.vout[vout], redeemScript)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Output does not match contract");
    }

    // Determine destination for refunded funds
    CTxDestination refundDest;
    KeyIO keyIO(Params());
    if (params.size() > 3 && !params[3].isNull()) {
        refundDest = keyIO.DecodeDestination(params[3].get_str());
        if (!IsValidDestination(refundDest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid refund address");
        }
    } else {
        // Use initiator's address by default
        refundDest = initiatorKeyID;
    }

    // Create refund transaction
    CMutableTransaction mtx;
    mtx.nVersion = SAPLING_TX_VERSION;
    mtx.fOverwintered = true;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
    mtx.nExpiryHeight = chainActive.Height() + 20;
    mtx.nLockTime = contract.lockTime; // Set locktime for CLTV

    // Add input spending the contract with nSequence that enables locktime
    mtx.vin.push_back(CTxIn(COutPoint(txid, vout), CScript(), std::numeric_limits<unsigned int>::max() - 1));

    // Calculate output amount (input amount minus fee)
    CAmount inputAmount = contractTx.vout[vout].nValue;
    CAmount fee = 10000; // 0.0001 YEC fee
    CAmount outputAmount = inputAmount - fee;

    if (outputAmount <= 0) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Contract value too small to pay fee");
    }

    // Add output
    CScript outputScript = GetScriptForDestination(refundDest);
    mtx.vout.push_back(CTxOut(outputAmount, outputScript));

    // Sign the transaction
    CTransaction txConst(mtx);

    // Get consensus branch ID for current height
    auto consensusBranchId = CurrentEpochBranchId(chainActive.Height(), Params().GetConsensus());

    uint256 hash = SignatureHash(redeemScript, txConst, 0, SIGHASH_ALL, inputAmount, consensusBranchId);

    vector<unsigned char> vchSig;
    if (!initiatorKey.Sign(hash, vchSig)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign transaction");
    }
    vchSig.push_back((unsigned char)SIGHASH_ALL);

    // Build scriptSig for refund
    vector<unsigned char> initiatorPubKeyVec(initiatorPubKey.begin(), initiatorPubKey.end());
    mtx.vin[0].scriptSig = BuildAtomicSwapRefundScript(vchSig, initiatorPubKeyVec, redeemScript);

    // Broadcast transaction
    CTransaction refundTx(mtx);
    CValidationState state;
    bool fMissingInputs;
    if (!AcceptToMemoryPool(Params(), mempool, state, refundTx, false, &fMissingInputs, true)) {
        if (state.IsInvalid()) {
            throw JSONRPCError(RPC_TRANSACTION_REJECTED,
                strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
        } else {
            if (fMissingInputs) {
                throw JSONRPCError(RPC_TRANSACTION_ERROR, "Missing inputs");
            }
            throw JSONRPCError(RPC_TRANSACTION_ERROR, state.GetRejectReason());
        }
    }

    RelayTransaction(refundTx);

    // Update swap status in database
    std::string swapId = txid.GetHex() + ":" + std::to_string(vout);
    CAtomicSwapInfo swapInfo;
    if (pwalletMain->GetAtomicSwap(swapId, swapInfo)) {
        // Update existing swap record
        swapInfo.status = SWAP_REFUNDED;
        swapInfo.completedTime = GetTime();
        swapInfo.spendTxid = refundTx.GetHash();
        if (!pwalletMain->UpdateAtomicSwap(swapInfo)) {
            LogPrintf("Warning: Failed to update atomic swap status: %s\n", swapId);
        }
    } else {
        // Log warning - refund should only happen for initiated swaps
        LogPrintf("Warning: Refunding swap not found in database: %s\n", swapId);
    }

    // Return result
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", refundTx.GetHash().GetHex());
    result.pushKV("hex", EncodeHexTx(refundTx));

    return result;
}

/**
 * listatomicswaps ( "status" )
 *
 * List all atomic swaps in the wallet.
 *
 * Arguments:
 * 1. status          (string, optional) Filter by status: "initiated", "participated", "claimed", "refunded", "expired", "abandoned"
 *
 * Result:
 * [
 *   {
 *     "swapId": "txid:vout",           // Unique swap identifier
 *     "contractTxid": "hex",           // Contract transaction ID
 *     "contractVout": n,               // Contract output index
 *     "amount": n.nnn,                 // Amount in YEC
 *     "role": "initiator|participant", // Our role in the swap
 *     "status": "...",                 // Current status
 *     "initiatedTime": n,              // When swap was initiated
 *     "completedTime": n,              // When swap completed (0 if not completed)
 *     "secretHash": "hex",             // Hash of the secret
 *     "secretKnown": true|false,       // Whether we know the secret
 *     "recipientAddress": "address",   // Recipient address
 *     "initiatorAddress": "address",   // Initiator address
 *     "locktime": n,                   // Refund locktime
 *     "spendTxid": "hex",              // Claim/refund txid (if spent)
 *     "label": "...",                  // User label
 *     "counterparty": "..."            // Counterparty identifier
 *   },
 *   ...
 * ]
 */
UniValue listatomicswaps(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw runtime_error(
            "listatomicswaps ( \"status\" )\n"
            "\nList all atomic swaps in the wallet.\n"
            "\nArguments:\n"
            "1. status          (string, optional) Filter by status: \"initiated\", \"participated\", \"claimed\", \"refunded\", \"expired\", \"abandoned\"\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"swapId\": \"txid:vout\",\n"
            "    \"contractTxid\": \"hex\",\n"
            "    \"contractVout\": n,\n"
            "    \"amount\": n.nnn,\n"
            "    \"role\": \"initiator|participant\",\n"
            "    \"status\": \"...\",\n"
            "    \"initiatedTime\": n,\n"
            "    \"completedTime\": n,\n"
            "    \"secretHash\": \"hex\",\n"
            "    \"secretKnown\": true|false,\n"
            "    \"contract\": \"hex\",          (string) The redeem script in hex\n"
            "    \"recipientAddress\": \"address\",\n"
            "    \"initiatorAddress\": \"address\",\n"
            "    \"locktime\": n,\n"
            "    \"spendTxid\": \"hex\",\n"
            "    \"label\": \"...\",\n"
            "    \"counterparty\": \"...\"\n"
            "  }\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("listatomicswaps", "")
            + HelpExampleCli("listatomicswaps", "\"initiated\"")
            + HelpExampleRpc("listatomicswaps", "\"claimed\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Get filter status if provided
    std::string filterStatus;
    if (params.size() > 0) {
        filterStatus = params[0].get_str();
    }

    // Get all swaps from wallet
    std::vector<CAtomicSwapInfo> swaps = pwalletMain->ListAtomicSwaps();

    // Build result array
    UniValue result(UniValue::VARR);
    for (const auto& swap : swaps) {
        // Apply status filter if provided
        if (!filterStatus.empty() && swap.GetStatusString() != filterStatus) {
            continue;
        }

        UniValue swapObj(UniValue::VOBJ);
        swapObj.pushKV("swapId", swap.GetSwapId());
        swapObj.pushKV("contractTxid", swap.contractTxid.GetHex());
        swapObj.pushKV("contractVout", (int)swap.contractVout);
        swapObj.pushKV("amount", ValueFromAmount(swap.amount));
        swapObj.pushKV("role", swap.GetRoleString());
        swapObj.pushKV("status", swap.GetStatusString());
        swapObj.pushKV("initiatedTime", swap.initiatedTime);
        if (swap.initiatedTime > 0) {
            swapObj.pushKV("initiatedTimeFormatted", DateTimeStrFormat("%Y-%m-%d %H:%M:%S UTC", swap.initiatedTime));
        }
        swapObj.pushKV("completedTime", swap.completedTime);
        if (swap.completedTime > 0) {
            swapObj.pushKV("completedTimeFormatted", DateTimeStrFormat("%Y-%m-%d %H:%M:%S UTC", swap.completedTime));
        }
        swapObj.pushKV("secretHash", swap.contract.secretHash.GetHex());
        swapObj.pushKV("secretKnown", swap.secretKnown);
        swapObj.pushKV("contract", HexStr(swap.redeemScript.begin(), swap.redeemScript.end()));

        // Get addresses from pubkeys
        CKeyID recipientKeyID = swap.contract.recipientPubKeyHash;
        CKeyID initiatorKeyID = swap.contract.initiatorPubKeyHash;
        KeyIO keyIO(Params());

        // Encode addresses as P2PKH destinations
        CTxDestination recipientDest(recipientKeyID);
        CTxDestination initiatorDest(initiatorKeyID);

        swapObj.pushKV("recipientAddress", keyIO.EncodeDestination(recipientDest));
        swapObj.pushKV("initiatorAddress", keyIO.EncodeDestination(initiatorDest));

        swapObj.pushKV("locktime", swap.contract.lockTime);
        // Add human-readable locktime if it's a Unix timestamp (>= 500000000)
        if (swap.contract.lockTime >= 500000000) {
            swapObj.pushKV("locktimeFormatted", DateTimeStrFormat("%Y-%m-%d %H:%M:%S UTC", swap.contract.lockTime));
        }

        // Add time until expiration for active swaps (not claimed, not refunded, not expired)
        if (swap.status == SWAP_INITIATED || swap.status == SWAP_PARTICIPATED) {
            int64_t timeUntilExpiry = 0;
            bool hasExpired = false;

            if (swap.contract.lockTime < 500000000) {
                // Block height based locktime
                int currentHeight = chainActive.Height();
                if (currentHeight >= swap.contract.lockTime) {
                    hasExpired = true;
                } else {
                    timeUntilExpiry = (swap.contract.lockTime - currentHeight) * 150; // ~150 seconds per block
                }
            } else {
                // Unix timestamp based locktime
                int64_t currentTime = GetTime();
                if (currentTime >= swap.contract.lockTime) {
                    hasExpired = true;
                } else {
                    timeUntilExpiry = swap.contract.lockTime - currentTime;
                }
            }

            if (!hasExpired && timeUntilExpiry > 0) {
                // Convert seconds to human-readable format
                int days = timeUntilExpiry / 86400;
                int hours = (timeUntilExpiry % 86400) / 3600;
                int minutes = (timeUntilExpiry % 3600) / 60;
                int seconds = timeUntilExpiry % 60;

                std::string timeStr;
                if (days > 0) {
                    timeStr = strprintf("%d day%s, %d hour%s",
                        days, (days != 1 ? "s" : ""),
                        hours, (hours != 1 ? "s" : ""));
                } else if (hours > 0) {
                    timeStr = strprintf("%d hour%s, %d minute%s",
                        hours, (hours != 1 ? "s" : ""),
                        minutes, (minutes != 1 ? "s" : ""));
                } else if (minutes > 0) {
                    timeStr = strprintf("%d minute%s, %d second%s",
                        minutes, (minutes != 1 ? "s" : ""),
                        seconds, (seconds != 1 ? "s" : ""));
                } else {
                    timeStr = strprintf("%d second%s",
                        seconds, (seconds != 1 ? "s" : ""));
                }

                swapObj.pushKV("timeUntilExpiry", timeStr);
                swapObj.pushKV("secondsUntilExpiry", timeUntilExpiry);
            }
        }

        // Add spending transaction details if the swap has been spent
        if (!swap.spendTxid.IsNull()) {
            swapObj.pushKV("spendTxid", swap.spendTxid.GetHex());

            // Get the spending transaction to find its block height
            CTransaction spendTx;
            uint256 spendBlockHash;
            if (GetTransaction(swap.spendTxid, spendTx, Params().GetConsensus(), spendBlockHash, true)) {
                if (!spendBlockHash.IsNull()) {
                    // Transaction is confirmed
                    CBlockIndex* pindexSpend = mapBlockIndex.count(spendBlockHash) ? mapBlockIndex[spendBlockHash] : nullptr;
                    if (pindexSpend) {
                        swapObj.pushKV("spendBlockHeight", pindexSpend->nHeight);
                        swapObj.pushKV("spendBlockTime", (int)pindexSpend->GetBlockTime());
                        swapObj.pushKV("spendBlockTimeFormatted",
                            DateTimeStrFormat("%Y-%m-%d %H:%M:%S UTC", pindexSpend->GetBlockTime()));
                    }
                } else {
                    // Transaction is in mempool, not yet confirmed
                    swapObj.pushKV("spendConfirmed", false);
                    swapObj.pushKV("spendStatus", "pending in mempool");
                }
            }
        }

        if (!swap.label.empty()) {
            swapObj.pushKV("label", swap.label);
        }

        if (!swap.counterparty.empty()) {
            swapObj.pushKV("counterparty", swap.counterparty);
        }

        result.push_back(swapObj);
    }

    return result;
}

/**
 * participateswap "contract" "contracttxid" vout "counterpartyaddress" ( "label" )
 *
 * Register interest in an atomic swap contract as a participant (typically the recipient).
 * This allows you to monitor when funds are claimed or refunded.
 *
 * Arguments:
 * 1. contract               (string, required) The HTLC contract (redeem script) in hex
 * 2. contracttxid           (string, required) The transaction ID of the contract
 * 3. vout                   (numeric, required) The output index
 * 4. counterpartyaddress    (string, required) Address of the counterparty (typically initiator)
 * 5. label                  (string, optional) User-defined label for the swap
 *
 * Result:
 * {
 *   "swapId": "txid:vout",           (string) The swap ID
 *   "status": "participated",        (string) Initial status
 *   "contractTxid": "hex",           (string) The contract transaction ID
 *   "contractVout": n,               (numeric) The output index
 *   "recipientAddress": "address",   (string) Your address (if determinable from wallet)
 *   "initiatorAddress": "address",   (string) The initiator's address
 * }
 */
UniValue participateswap(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 4 || params.size() > 5)
        throw runtime_error(
            "participateswap \"contract\" \"contracttxid\" vout \"counterpartyaddress\" ( \"label\" )\n"
            "\nRegister interest in an atomic swap contract as a participant.\n"
            "\nArguments:\n"
            "1. \"contract\"              (string, required) The HTLC contract (redeem script) in hex\n"
            "2. \"contracttxid\"          (string, required) The transaction ID of the contract\n"
            "3. vout                     (numeric, required) The output index\n"
            "4. \"counterpartyaddress\"   (string, required) Address of the counterparty\n"
            "5. \"label\"                 (string, optional) User-defined label for this swap\n"
            "\nResult:\n"
            "{\n"
            "  \"swapId\": \"txid:vout\",           (string) The swap ID\n"
            "  \"status\": \"participated\",        (string) Initial status\n"
            "  \"contractTxid\": \"hex\",           (string) The contract transaction ID\n"
            "  \"contractVout\": n,                 (numeric) The output index\n"
            "  \"recipientAddress\": \"address\",   (string) Your address\n"
            "  \"initiatorAddress\": \"address\"    (string) The initiator's address\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("participateswap", "\"6382012088a820...\" \"abc123...\" 0 \"s1YourAddressHere\"")
            + HelpExampleRpc("participateswap", "\"6382012088a820...\", \"abc123...\", 0, \"s1YourAddressHere\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Parse contract (redeem script)
    string contractHex = params[0].get_str();
    if (!IsHex(contractHex)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Contract must be hex encoded");
    }
    vector<unsigned char> contractData = ParseHex(contractHex);
    CScript redeemScript(contractData.begin(), contractData.end());

    // Extract contract info
    AtomicSwapContract contract;
    if (!ExtractAtomicSwapContract(redeemScript, contract)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid atomic swap contract");
    }

    // Parse transaction ID
    uint256 txid;
    txid.SetHex(params[1].get_str());

    // Parse vout
    uint32_t vout = params[2].get_int();

    // Parse counterparty address (optional, just for user reference)
    string counterparty = params[3].get_str();

    // Parse label (optional)
    string label;
    if (params.size() > 4) {
        label = params[4].get_str();
    }

    // Get the transaction to verify it exists
    CTransaction tx;
    uint256 blockHash;
    if (!GetTransaction(txid, tx, Params().GetConsensus(), blockHash, true)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not found");
    }

    // Verify vout is valid
    if (vout >= tx.vout.size()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Output index out of range");
    }

    // Verify the output
    if (!VerifyAtomicSwapOutput(tx.vout[vout], redeemScript)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Output does not match contract");
    }

    // Create swap info record
    CAmount amount = tx.vout[vout].nValue;
    CAtomicSwapInfo swapInfo(txid, vout, redeemScript, amount, contract, ROLE_PARTICIPANT);
    swapInfo.label = label;
    swapInfo.counterparty = counterparty;

    // Persist to wallet
    if (!pwalletMain->AddAtomicSwap(swapInfo)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to register swap in wallet");
    }

    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("swapId", swapInfo.GetSwapId());
    result.pushKV("status", "participated");
    result.pushKV("contractTxid", txid.GetHex());
    result.pushKV("contractVout", (int)vout);
    result.pushKV("amount", ValueFromAmount(amount));

    // Get addresses from contract
    KeyIO keyIO(Params());
    CKeyID recipientKeyID = contract.recipientPubKeyHash;
    CKeyID initiatorKeyID = contract.initiatorPubKeyHash;

    CTxDestination recipientDest(recipientKeyID);
    CTxDestination initiatorDest(initiatorKeyID);

    result.pushKV("recipientAddress", keyIO.EncodeDestination(recipientDest));
    result.pushKV("initiatorAddress", keyIO.EncodeDestination(initiatorDest));
    result.pushKV("secretHash", contract.secretHash.GetHex());

    if (!label.empty()) {
        result.pushKV("label", label);
    }

    return result;
}

/**
 * getswapsecret "swapid"
 *
 * Retrieve the secret for an atomic swap if it is known.
 * Only works for swaps you initiated or have claimed/discovered the secret for.
 *
 * Arguments:
 * 1. swapid  (string, required) The swap ID in format "txid:vout"
 *
 * Result:
 * {
 *   "secret": "hex",     (string) The secret preimage in hex
 *   "secretHash": "hex", (string) The HASH160 of the secret
 *   "known": true        (boolean) Always true if this command succeeds
 * }
 */
UniValue getswapsecret(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getswapsecret \"swapid\"\n"
            "\nRetrieve the secret for an atomic swap if it is known.\n"
            "\nArguments:\n"
            "1. \"swapid\"  (string, required) The swap ID in format \"txid:vout\"\n"
            "\nResult:\n"
            "{\n"
            "  \"secret\": \"hex\",     (string) The secret preimage in hex\n"
            "  \"secretHash\": \"hex\", (string) The HASH160 of the secret\n"
            "  \"known\": true          (boolean) Always true if this command succeeds\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getswapsecret", "\"abc123...:0\"")
            + HelpExampleRpc("getswapsecret", "\"abc123...:0\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Parse swap ID
    std::string swapId = params[0].get_str();

    // Get the swap from wallet
    CAtomicSwapInfo swapInfo;
    if (!pwalletMain->GetAtomicSwap(swapId, swapInfo)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Swap not found: " + swapId);
    }

    // Check if secret is known
    if (!swapInfo.secretKnown || swapInfo.secret.empty()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Secret is not known for this swap");
    }

    // Return result
    UniValue result(UniValue::VOBJ);
    result.pushKV("secret", HexStr(swapInfo.secret.begin(), swapInfo.secret.end()));
    result.pushKV("secretHash", swapInfo.contract.secretHash.GetHex());
    result.pushKV("known", true);

    return result;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "atomicswap",         "initiateswap",           &initiateswap,           false },
    { "atomicswap",         "initiateswapfromhash",   &initiateswapfromhash,   false },
    { "atomicswap",         "auditswap",              &auditswap,              true  },
    { "atomicswap",         "participateswap",        &participateswap,        false },
    { "atomicswap",         "claimswap",              &claimswap,              false },
    { "atomicswap",         "refundswap",             &refundswap,             false },
    { "atomicswap",         "listatomicswaps",        &listatomicswaps,        true  },
    { "atomicswap",         "getswapsecret",          &getswapsecret,          false },
};

void RegisterAtomicSwapRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

#ifdef ENABLE_WALLET
void InitAtomicSwapDatabase()
{
    // Atomic swaps are now stored in wallet.dat, no separate database needed
    if (!fExperimentalAtomicSwaps)
        return;

    LogPrintf("Atomic swap support enabled (stored in wallet.dat)\n");
}

void ShutdownAtomicSwapDatabase()
{
    // Nothing to do - wallet handles persistence
}

/**
 * Monitor a transaction for atomic swap spending
 * Called when a transaction enters the mempool or is added to a block
 *
 * This function detects when registered swaps are spent (claimed or refunded)
 * by extracting and matching the redeemScript from the spending transaction.
 */
void MonitorAtomicSwapTransaction(const CTransaction& tx)
{
    if (!fExperimentalAtomicSwaps)
        return;
    if (!pwalletMain)
        return;

    // Get all registered swaps from wallet
    std::vector<CAtomicSwapInfo> swaps = pwalletMain->ListAtomicSwaps();

    // Check each input to see if it spends any of our registered swaps
    for (const CTxIn& txin : tx.vin) {
        // Extract the redeemScript from the scriptSig
        // Claim transactions have format: <sig> <pubkey> <secret> <OP_TRUE> <redeemScript>
        // Refund transactions have format: <sig> <OP_FALSE> <redeemScript>
        // In both cases, the last push in scriptSig is the redeemScript

        // Parse scriptSig as a stack of pushes
        std::vector<std::vector<unsigned char>> scriptStack;
        CScript::const_iterator it = txin.scriptSig.begin();
        opcodetype opcode;
        while (it < txin.scriptSig.end()) {
            std::vector<unsigned char> pushData;
            if (!txin.scriptSig.GetOp(it, opcode, pushData)) {
                break;
            }
            if (pushData.size() > 0) {
                scriptStack.push_back(pushData);
            }
        }

        // Check if last element might be a redeemScript
        if (scriptStack.empty())
            continue;

        const std::vector<unsigned char>& lastPush = scriptStack.back();
        CScript redeemScript(lastPush.begin(), lastPush.end());

        // Check if this redeemScript matches any of our registered swaps
        for (const CAtomicSwapInfo& swap : swaps) {
            // Skip already completed swaps
            if (swap.status == SWAP_CLAIMED || swap.status == SWAP_REFUNDED ||
                swap.status == SWAP_EXPIRED || swap.status == SWAP_ABANDONED)
                continue;

            // Skip if we already detected this spend
            if (!swap.spendTxid.IsNull() && swap.spendTxid == tx.GetHash())
                continue;

            // Compare redeemScripts
            if (redeemScript != swap.redeemScript)
                continue;

            // Found a match! This spending transaction is for our registered swap
            CAtomicSwapInfo updatedSwap = swap;
            updatedSwap.spendTxid = tx.GetHash();
            updatedSwap.completedTime = GetTime();

            // Try to extract secret to determine if this is a claim or refund
            std::vector<unsigned char> secret;
            if (ExtractAtomicSwapSecret(txin.scriptSig, secret)) {
                // Claim transaction - secret revealed
                updatedSwap.secret = secret;
                updatedSwap.secretKnown = true;
                updatedSwap.status = SWAP_CLAIMED;
                pwalletMain->UpdateAtomicSwap(updatedSwap);

                if (swap.role == ROLE_INITIATOR) {
                    LogPrintf("Atomic swap monitor: Initiated swap %s CLAIMED by recipient in tx %s (secret revealed)\n",
                              swap.GetSwapId(), tx.GetHash().GetHex());
                } else {
                    LogPrintf("Atomic swap monitor: Participated swap %s CLAIMED in tx %s (secret revealed)\n",
                              swap.GetSwapId(), tx.GetHash().GetHex());
                }
            } else {
                // Refund transaction - no secret revealed
                updatedSwap.status = SWAP_REFUNDED;
                pwalletMain->UpdateAtomicSwap(updatedSwap);
                LogPrintf("Atomic swap monitor: Swap %s REFUNDED in tx %s\n",
                          swap.GetSwapId(), tx.GetHash().GetHex());
            }
            break;
        }
    }
}

/**
 * Check all active swaps for expiration
 * Should be called periodically (e.g., on new blocks)
 */
void CheckAtomicSwapExpirations(int currentHeight, int64_t currentTime)
{
    if (!fExperimentalAtomicSwaps)
        return;
    if (!pwalletMain)
        return;

    std::vector<CAtomicSwapInfo> swaps = pwalletMain->ListAtomicSwaps();
    for (const CAtomicSwapInfo& swap : swaps) {
        // Skip completed swaps
        if (swap.status == SWAP_CLAIMED || swap.status == SWAP_REFUNDED || swap.status == SWAP_ABANDONED)
            continue;

        // Check if swap has expired
        if (swap.HasExpired(currentHeight, currentTime)) {
            if (swap.status != SWAP_EXPIRED) {
                CAtomicSwapInfo updatedSwap = swap;
                updatedSwap.status = SWAP_EXPIRED;
                pwalletMain->UpdateAtomicSwap(updatedSwap);
                LogPrintf("Atomic swap %s has expired\n", swap.GetSwapId());
            }
        }
    }
}

/**
 * Handle atomic swap disconnect due to chain reorganization
 * Called when a transaction is disconnected from the chain
 * Reverts swap status if the transaction was spending a swap
 */
void HandleAtomicSwapDisconnect(const CTransaction& tx)
{
    if (!fExperimentalAtomicSwaps)
        return;
    if (!pwalletMain)
        return;

    // Check each swap in the wallet to see if this transaction was spending it
    std::vector<CAtomicSwapInfo> swaps = pwalletMain->ListAtomicSwaps();
    for (const CAtomicSwapInfo& swap : swaps) {
        // Check if this transaction spent the swap
        for (const CTxIn& txin : tx.vin) {
            if (txin.prevout.hash == swap.contractTxid && txin.prevout.n == swap.contractVout) {
                // Check if this transaction was the spending transaction we recorded
                if (swap.spendTxid == tx.GetHash()) {
                    // Revert the swap status
                    CAtomicSwapInfo updatedSwap = swap;

                    // Keep the secret if it's known, but revert the spending status
                    if (updatedSwap.role == ROLE_INITIATOR) {
                        // For initiated swaps, go back to SWAP_INITIATED
                        updatedSwap.status = SWAP_INITIATED;
                    } else {
                        // For participant swaps, go back to SWAP_PARTICIPATED
                        updatedSwap.status = SWAP_PARTICIPATED;
                    }

                    // Clear the spend transaction reference but keep the secret
                    updatedSwap.spendTxid.SetNull();
                    updatedSwap.completedTime = 0;

                    pwalletMain->UpdateAtomicSwap(updatedSwap);
                    LogPrintf("Atomic swap monitor: Swap %s disconnected due to reorg (txid: %s)\n",
                              swap.GetSwapId(), tx.GetHash().GetHex());
                }
                break;
            }
        }
    }
}
#endif
