// Copyright (c) 2025 The Ycash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef BITCOIN_SCRIPT_ATOMICSWAP_H
#define BITCOIN_SCRIPT_ATOMICSWAP_H

#include "script/script.h"
#include "script/standard.h"
#include "primitives/transaction.h"
#include "pubkey.h"
#include "uint256.h"

#include <optional>
#include <vector>

/**
 * Atomic Swap HTLC (Hash Time-Locked Contract) Implementation
 *
 * This implements cross-chain atomic swaps using hash time-locked contracts.
 * The contract allows two parties to exchange funds across different blockchains
 * without requiring trust, using a shared secret and timelocks.
 *
 * HTLC Script Structure:
 *
 * IF
 *   // Claim path: recipient provides preimage
 *   HASH160 <secret_hash> EQUALVERIFY DUP HASH160 <recipient_pubkey_hash> EQUALVERIFY CHECKSIG
 * ELSE
 *   // Refund path: initiator reclaims after locktime
 *   <locktime> CHECKLOCKTIMEVERIFY DROP DUP HASH160 <initiator_pubkey_hash> EQUALVERIFY CHECKSIG
 * ENDIF
 *
 * The contract has two spending paths:
 * 1. Claim: The recipient can claim funds by revealing the preimage (secret)
 * 2. Refund: The initiator can reclaim funds after the locktime expires
 */

/**
 * Atomic swap contract data structure
 */
struct AtomicSwapContract {
    uint160 secretHash;           // HASH160 of the secret (RIPEMD160(SHA256(secret)))
    CKeyID recipientPubKeyHash;   // Public key hash of the recipient who can claim
    CKeyID initiatorPubKeyHash;   // Public key hash of the initiator who can refund
    int64_t lockTime;             // Absolute locktime (block height or Unix timestamp)

    AtomicSwapContract() : lockTime(0) {}

    AtomicSwapContract(
        const uint160& secretHash_,
        const CKeyID& recipientPubKeyHash_,
        const CKeyID& initiatorPubKeyHash_,
        int64_t lockTime_)
        : secretHash(secretHash_),
          recipientPubKeyHash(recipientPubKeyHash_),
          initiatorPubKeyHash(initiatorPubKeyHash_),
          lockTime(lockTime_) {}

    bool IsValid() const;
};

/**
 * Build an HTLC redeem script for atomic swaps
 *
 * @param contract The atomic swap contract parameters
 * @return The HTLC redeem script (to be used in P2SH)
 */
CScript BuildAtomicSwapScript(const AtomicSwapContract& contract);

/**
 * Extract atomic swap contract data from a redeem script
 *
 * @param redeemScript The redeem script to parse
 * @param contractOut Output parameter for the parsed contract
 * @return true if successfully parsed, false otherwise
 */
bool ExtractAtomicSwapContract(const CScript& redeemScript, AtomicSwapContract& contractOut);

/**
 * Build a scriptSig to claim an atomic swap (reveal secret)
 *
 * @param secret The 32-byte secret preimage
 * @param signature The signature from the recipient
 * @param pubkey The public key of the recipient (required for P2PKH verification)
 * @param redeemScript The HTLC redeem script
 * @return The scriptSig for claiming
 */
CScript BuildAtomicSwapClaimScript(
    const std::vector<unsigned char>& secret,
    const std::vector<unsigned char>& signature,
    const std::vector<unsigned char>& pubkey,
    const CScript& redeemScript);

/**
 * Build a scriptSig to refund an atomic swap (after locktime)
 *
 * @param signature The signature from the initiator
 * @param pubkey The public key of the initiator (required for P2PKH verification)
 * @param redeemScript The HTLC redeem script
 * @return The scriptSig for refunding
 */
CScript BuildAtomicSwapRefundScript(
    const std::vector<unsigned char>& signature,
    const std::vector<unsigned char>& pubkey,
    const CScript& redeemScript);

/**
 * Verify that a transaction output contains a valid atomic swap contract
 *
 * @param txOut The transaction output to verify
 * @param redeemScript The expected redeem script
 * @return true if valid, false otherwise
 */
bool VerifyAtomicSwapOutput(const CTxOut& txOut, const CScript& redeemScript);

/**
 * Extract the secret from a claim transaction
 *
 * @param tx The transaction that claimed the swap
 * @param vin The input index that spent the swap
 * @param secretOut Output parameter for the extracted secret
 * @return true if secret was extracted, false otherwise
 */
bool ExtractAtomicSwapSecret(
    const CTransaction& tx,
    uint32_t vin,
    std::vector<unsigned char>& secretOut);

/**
 * Extract the secret directly from a scriptSig
 *
 * @param scriptSig The scriptSig from a claim transaction
 * @param secretOut Output parameter for the extracted secret
 * @return true if secret was extracted, false otherwise
 */
bool ExtractAtomicSwapSecret(
    const CScript& scriptSig,
    std::vector<unsigned char>& secretOut);

/**
 * Calculate HASH160 of data (RIPEMD160(SHA256(data)))
 * This matches Bitcoin's standard address hashing
 *
 * @param data Input data to hash
 * @return The 160-bit hash
 */
uint160 Hash160(const std::vector<unsigned char>& data);

/**
 * Generate a random 32-byte secret for atomic swaps
 *
 * @return A cryptographically secure random 32-byte secret
 */
std::vector<unsigned char> GenerateAtomicSwapSecret();

/**
 * Validate atomic swap parameters
 *
 * @param contract The contract to validate
 * @param currentHeight Current blockchain height (for locktime validation)
 * @param errorOut Output parameter for error message
 * @return true if valid, false otherwise
 */
bool ValidateAtomicSwapContract(
    const AtomicSwapContract& contract,
    int currentHeight,
    std::string& errorOut);

#endif // BITCOIN_SCRIPT_ATOMICSWAP_H
