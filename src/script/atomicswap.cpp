// Copyright (c) 2025 The Ycash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "script/atomicswap.h"
#include "script/script.h"
#include "script/standard.h"
#include "hash.h"
#include "random.h"
#include "utilstrencodings.h"
#include "utiltime.h"

#include <algorithm>
#include <logging.h>

bool AtomicSwapContract::IsValid() const
{
    return !secretHash.IsNull() &&
           !recipientPubKeyHash.IsNull() &&
           !initiatorPubKeyHash.IsNull() &&
           lockTime > 0;
}

CScript BuildAtomicSwapScript(const AtomicSwapContract& contract)
{
    CScript script;

    // IF branch: Claim by revealing secret
    script << OP_IF
           << OP_HASH160
           << ToByteVector(contract.secretHash)
           << OP_EQUALVERIFY
           << OP_DUP
           << OP_HASH160
           << ToByteVector(contract.recipientPubKeyHash)

    // ELSE branch: Refund after locktime
           << OP_ELSE
           << contract.lockTime
           << OP_CHECKLOCKTIMEVERIFY
           << OP_DROP
           << OP_DUP
           << OP_HASH160
           << ToByteVector(contract.initiatorPubKeyHash)

    // ENDIF - common signature verification
           << OP_ENDIF
           << OP_EQUALVERIFY
           << OP_CHECKSIG;

    return script;
}

bool ExtractAtomicSwapContract(const CScript& redeemScript, AtomicSwapContract& contractOut)
{
    CScript::const_iterator pc = redeemScript.begin();
    std::vector<unsigned char> data;
    opcodetype opcode;

    // Parse: OP_IF
    if (!redeemScript.GetOp(pc, opcode) || opcode != OP_IF)
        return false;

    // Parse: OP_HASH160
    if (!redeemScript.GetOp(pc, opcode) || opcode != OP_HASH160)
        return false;

    // Parse: <secret_hash> (20 bytes)
    if (!redeemScript.GetOp(pc, opcode, data) || data.size() != 20)
        return false;
    contractOut.secretHash = uint160(data);

    // Parse: OP_EQUALVERIFY
    if (!redeemScript.GetOp(pc, opcode) || opcode != OP_EQUALVERIFY)
        return false;

    // Parse: OP_DUP
    if (!redeemScript.GetOp(pc, opcode) || opcode != OP_DUP)
        return false;

    // Parse: OP_HASH160
    if (!redeemScript.GetOp(pc, opcode) || opcode != OP_HASH160)
        return false;

    // Parse: <recipient_pubkey_hash> (20 bytes)
    if (!redeemScript.GetOp(pc, opcode, data) || data.size() != 20)
        return false;
    contractOut.recipientPubKeyHash = CKeyID(uint160(data));

    // Parse: OP_ELSE
    if (!redeemScript.GetOp(pc, opcode) || opcode != OP_ELSE)
        return false;

    // Parse: <locktime>
    if (!redeemScript.GetOp(pc, opcode, data))
        return false;

    if (data.size() > 5)
        return false;
    contractOut.lockTime = CScriptNum(data, false).getint();

    // Parse: OP_CHECKLOCKTIMEVERIFY
    if (!redeemScript.GetOp(pc, opcode) || opcode != OP_CHECKLOCKTIMEVERIFY)
        return false;

    // Parse: OP_DROP
    if (!redeemScript.GetOp(pc, opcode) || opcode != OP_DROP)
        return false;

    // Parse: OP_DUP
    if (!redeemScript.GetOp(pc, opcode) || opcode != OP_DUP)
        return false;

    // Parse: OP_HASH160
    if (!redeemScript.GetOp(pc, opcode) || opcode != OP_HASH160)
        return false;

    // Parse: <initiator_pubkey_hash> (20 bytes)
    if (!redeemScript.GetOp(pc, opcode, data) || data.size() != 20)
        return false;
    contractOut.initiatorPubKeyHash = CKeyID(uint160(data));

    // Parse: OP_ENDIF
    if (!redeemScript.GetOp(pc, opcode) || opcode != OP_ENDIF)
        return false;

    // Parse: OP_EQUALVERIFY (moved after ENDIF)
    if (!redeemScript.GetOp(pc, opcode) || opcode != OP_EQUALVERIFY)
        return false;

    // Parse: OP_CHECKSIG (moved after ENDIF)
    if (!redeemScript.GetOp(pc, opcode) || opcode != OP_CHECKSIG)
        return false;

    // Should be at end of script
    if (pc != redeemScript.end())
        return false;

    return contractOut.IsValid();
}

CScript BuildAtomicSwapClaimScript(
    const std::vector<unsigned char>& secret,
    const std::vector<unsigned char>& signature,
    const std::vector<unsigned char>& pubkey,
    const CScript& redeemScript)
{
    CScript scriptSig;

    // Push signature
    scriptSig << signature;

    // Push public key (required for P2PKH-style verification in the HTLC script)
    scriptSig << pubkey;

    // Push secret (preimage)
    scriptSig << secret;

    // Push OP_TRUE to select the IF branch (claim path)
    scriptSig << OP_TRUE;

    // Push the redeem script
    scriptSig << std::vector<unsigned char>(redeemScript.begin(), redeemScript.end());

    return scriptSig;
}

CScript BuildAtomicSwapRefundScript(
    const std::vector<unsigned char>& signature,
    const std::vector<unsigned char>& pubkey,
    const CScript& redeemScript)
{
    CScript scriptSig;

    // Push signature
    scriptSig << signature;

    // Push public key (required for P2PKH-style verification in the HTLC script)
    scriptSig << pubkey;

    // Push OP_FALSE to select the ELSE branch (refund path)
    scriptSig << OP_FALSE;

    // Push the redeem script
    scriptSig << std::vector<unsigned char>(redeemScript.begin(), redeemScript.end());

    return scriptSig;
}

bool VerifyAtomicSwapOutput(const CTxOut& txOut, const CScript& redeemScript)
{
    // Calculate P2SH address from redeem script
    CScriptID scriptID(redeemScript);
    CScript expectedScriptPubKey = GetScriptForDestination(scriptID);

    // Verify the output's scriptPubKey matches
    return txOut.scriptPubKey == expectedScriptPubKey;
}

bool ExtractAtomicSwapSecret(
    const CTransaction& tx,
    uint32_t vin,
    std::vector<unsigned char>& secretOut)
{
    if (vin >= tx.vin.size())
        return false;

    return ExtractAtomicSwapSecret(tx.vin[vin].scriptSig, secretOut);
}

bool ExtractAtomicSwapSecret(
    const CScript& scriptSig,
    std::vector<unsigned char>& secretOut)
{
    CScript::const_iterator pc = scriptSig.begin();
    std::vector<unsigned char> data;
    opcodetype opcode;

    // scriptSig format for claim: <sig> <pubkey> <secret> <OP_TRUE> <redeemScript>
    // We need to extract the third element (secret)

    // Skip signature
    if (!scriptSig.GetOp(pc, opcode, data))
        return false;

    // Skip public key
    if (!scriptSig.GetOp(pc, opcode, data))
        return false;

    // Get secret
    if (!scriptSig.GetOp(pc, opcode, data))
        return false;

    // Verify it looks like OP_TRUE next (to confirm this is claim path)
    opcodetype nextOp;
    if (!scriptSig.GetOp(pc, nextOp) || nextOp != OP_TRUE)
        return false;

    secretOut = data;
    return !secretOut.empty();
}

std::vector<unsigned char> GenerateAtomicSwapSecret()
{
    std::vector<unsigned char> secret(32);
    GetRandBytes(secret.data(), 32);
    return secret;
}

bool ValidateAtomicSwapContract(
    const AtomicSwapContract& contract,
    int currentHeight,
    std::string& errorOut)
{
    if (contract.secretHash.IsNull()) {
        errorOut = "Secret hash is null";
        return false;
    }

    if (contract.recipientPubKeyHash.IsNull()) {
        errorOut = "Recipient public key hash is invalid";
        return false;
    }

    if (contract.initiatorPubKeyHash.IsNull()) {
        errorOut = "Initiator public key hash is invalid";
        return false;
    }

    if (contract.lockTime <= 0) {
        errorOut = "Locktime must be positive";
        return false;
    }

    // Validate locktime is reasonable
    // If locktime < 500000000, it's a block height
    // If locktime >= 500000000, it's a Unix timestamp
    if (contract.lockTime < 500000000) {
        // Block height based locktime
        if (contract.lockTime <= currentHeight) {
            errorOut = "Locktime must be in the future (current height: " +
                       std::to_string(currentHeight) + ", locktime: " +
                       std::to_string(contract.lockTime) + ")";
            return false;
        }

        // Require at least a reasonable number of blocks in the future
        if (contract.lockTime < currentHeight + 10) {
            errorOut = "Locktime must be at least 10 blocks in the future";
            return false;
        }
    } else {
        // Unix timestamp based locktime
        int64_t currentTime = GetTime();
        if (contract.lockTime <= currentTime) {
            errorOut = "Locktime must be in the future";
            return false;
        }

        // Require at least 1 hour in the future
        // if (contract.lockTime < currentTime + 3600) {
        if (contract.lockTime < currentTime + 600) {
            // errorOut = "Locktime must be at least 1 hour in the future";
            errorOut = "Locktime must be at least 10 minutes in the future";
            return false;
        }
    }

    // Validate public key hashes are different
    if (contract.recipientPubKeyHash == contract.initiatorPubKeyHash) {
        errorOut = "Recipient and initiator must have different public key hashes";
        return false;
    }

    return true;
}
