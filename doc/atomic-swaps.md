# Atomic Swap Implementation for Ycashd

## Overview

This document describes the atomic swap functionality implemented in ycashd, which enables trustless cross-chain cryptocurrency exchanges using Hash Time-Locked Contracts (HTLCs).

## What are Atomic Swaps?

Atomic swaps allow two parties to exchange cryptocurrencies across different blockchains without requiring a trusted third party. The exchange is "atomic" - it either completes in full or not at all, ensuring neither party can cheat the other.

## How It Works

### HTLC (Hash Time-Locked Contract)

The implementation uses a Pay-to-Script-Hash (P2SH) contract with two spending conditions:

```
IF
  // Claim path: recipient reveals secret
  HASH160 <secret_hash> EQUALVERIFY DUP HASH160 <recipient_pubkey_hash> EQUALVERIFY CHECKSIG
ELSE
  // Refund path: initiator reclaims after timeout
  <locktime> CHECKLOCKTIMEVERIFY DROP DUP HASH160 <initiator_pubkey_hash> EQUALVERIFY CHECKSIG
ENDIF
```

Note: Both branches use P2PKH-style verification (DUP HASH160 <pubkey_hash> EQUALVERIFY CHECKSIG), requiring the spender to provide both signature and public key in the scriptSig.

**Two spending paths:**
1. **Claim**: The recipient can claim funds by revealing the preimage (secret)
2. **Refund**: The initiator can reclaim funds after the locktime expires

## RPC Commands

### `initiateswap`

Initiate an atomic swap by creating an HTLC contract.

**Syntax:**
```
initiateswap "fundingaddress" "recipientaddress" amount locktime ( "secret" )
```

**Arguments:**
- `fundingaddress` (string, required): Existing wallet address with sufficient balance to fund the swap
- `recipientaddress` (string, required): The Ycash address of the recipient
- `amount` (numeric, required): The amount in YEC to lock
- `locktime` (numeric, required): Absolute locktime (block height < 500000000 or Unix timestamp >= 500000000)
- `secret` (string, optional): 32-byte secret in hex (generated if not provided)

**Returns:**
```json
{
  "contract": "hex",                    // The HTLC contract (redeem script) in hex
  "contractP2SH": "address",            // The P2SH address for the contract
  "contractTxid": "hex",                // The transaction ID of the contract
  "contractVout": n,                    // The output index
  "secret": "hex",                      // The secret preimage in hex
  "secretHash": "hex",                  // The HASH160 of the secret
  "refundLocktime": n,                  // The locktime for refunds
  "refundLocktimeFormatted": "..."      // Human-readable locktime (only if Unix timestamp)
}
```

**Example:**
```bash
ycash-cli initiateswap "s1FundingAddressHere" "s1cXmTZneHwrWZ2jzzUcvpjHgLC6njnnnnn" 1.0 550000
```

### `initiateswapfromhash`

Initiate an atomic swap using a known secret hash (when you are the participant, not the secret generator).

**Syntax:**
```
initiateswapfromhash "fundingaddress" "recipientaddress" amount locktime "secrethash"
```

**Arguments:**
- `fundingaddress` (string, required): Existing wallet address with sufficient balance to fund the swap
- `recipientaddress` (string, required): The Ycash address of the recipient
- `amount` (numeric, required): The amount in YEC to lock
- `locktime` (numeric, required): Absolute locktime (block height < 500000000 or Unix timestamp >= 500000000)
- `secrethash` (string, required): The 20-byte HASH160 of the secret in hex (40 hex characters)

**Returns:**
```json
{
  "contract": "hex",                    // The HTLC contract (redeem script) in hex
  "contractP2SH": "address",            // The P2SH address for the contract
  "contractTxid": "hex",                // The transaction ID of the contract
  "contractVout": n,                    // The output index
  "secretHash": "hex",                  // The HASH160 of the secret
  "refundLocktime": n,                  // The locktime for refunds
  "refundLocktimeFormatted": "..."      // Human-readable locktime (only if Unix timestamp)
}
```

**Example:**
```bash
ycash-cli initiateswapfromhash "s1FundingAddressHere" "s1cXmTZneHwrWZ2jzzUcvpjHgLC6njnnnnn" 1.0 551000 "9876543210fedcba..."
```

**Note on Endianness:** The secret hash should be provided as a big-endian hex string (40 hex characters). This is the standard output format from HASH160 operations on other blockchains and in JavaScript/Python tools. Ycashd will handle the internal conversion to little-endian storage.

### `auditswap`

Audit an atomic swap contract to verify its parameters.

**Syntax:**
```
auditswap "contract" "contracttxid" vout
```

**Arguments:**
- `contract` (string, required): The HTLC contract (redeem script) in hex
- `contracttxid` (string, required): The transaction ID of the contract
- `vout` (numeric, required): The output index

**Returns:**
```json
{
  "contractP2SH": "address",            // The P2SH address for the contract
  "contractValue": n,                   // The value locked in the contract
  "recipientAddress": "address",        // The address that can claim
  "initiatorAddress": "address",        // The address that can refund
  "secretHash": "hex",                  // The HASH160 of the secret
  "refundLocktime": n,                  // The locktime for refunds
  "refundLocktimeFormatted": "...",     // Human-readable locktime (only if Unix timestamp)
  "locktimeReached": true|false         // Whether the locktime has been reached
}
```

**Example:**
```bash
ycash-cli auditswap "6382012088a820..." "abc123..." 0
```

### `claimswap`

Claim funds from an atomic swap by revealing the secret.

**Syntax:**
```
claimswap "contract" "contracttxid" vout "secret" ( "recipientaddress" )
```

**Arguments:**
- `contract` (string, required): The HTLC contract (redeem script) in hex
- `contracttxid` (string, required): The transaction ID of the contract
- `vout` (numeric, required): The output index
- `secret` (string, required): The 32-byte secret in hex
- `recipientaddress` (string, optional): The address to send claimed funds to

**Returns:**
```json
{
  "txid": "hex",     // The claim transaction ID
  "hex": "hex"       // The raw claim transaction in hex
}
```

**Example:**
```bash
ycash-cli claimswap "6382012088a820..." "abc123..." 0 "def456..."
```

### `refundswap`

Refund an atomic swap after the locktime has expired.

**Syntax:**
```
refundswap "contract" "contracttxid" vout ( "refundaddress" )
```

**Arguments:**
- `contract` (string, required): The HTLC contract (redeem script) in hex
- `contracttxid` (string, required): The transaction ID of the contract
- `vout` (numeric, required): The output index
- `refundaddress` (string, optional): The address to send refunded funds to

**Returns:**
```json
{
  "txid": "hex",     // The refund transaction ID
  "hex": "hex"       // The raw refund transaction in hex
}
```

**Example:**
```bash
ycash-cli refundswap "6382012088a820..." "abc123..." 0
```

### `listatomicswaps`

List all atomic swaps stored in the wallet.

**Syntax:**
```
listatomicswaps ( "status" )
```

**Arguments:**
- `status` (string, optional): Filter by status: "initiated", "participated", "claimed", "refunded", "expired", "abandoned"

**Returns:**
```json
[
  {
    "swapId": "txid:vout",                  // Unique swap identifier
    "contractTxid": "hex",                  // The transaction ID of the contract
    "contractVout": n,                      // The output index
    "amount": n.nnn,                        // The amount locked in YEC
    "role": "initiator|participant",        // Your role in the swap
    "status": "...",                        // Current status
    "initiatedTime": n,                     // Unix timestamp when initiated
    "initiatedTimeFormatted": "...",        // Human-readable initiated time
    "completedTime": n,                     // Unix timestamp when completed (if applicable)
    "completedTimeFormatted": "...",        // Human-readable completed time (if applicable)
    "secretHash": "hex",                    // The HASH160 of the secret
    "secretKnown": true|false,              // Whether the secret is known
    "contract": "hex",                      // The redeem script in hex
    "recipientAddress": "address",          // The address that can claim
    "initiatorAddress": "address",          // The address that can refund
    "locktime": n,                          // The locktime for refunds
    "locktimeFormatted": "...",             // Human-readable locktime (only if Unix timestamp)
    "timeUntilExpiry": "...",               // Human-readable time until expiry (for active swaps)
    "secondsUntilExpiry": n,                // Seconds until expiry (for active swaps)
    "spendTxid": "hex",                     // The txid that spent the contract (if claimed/refunded)
    "spendBlockHeight": n,                  // Block height of spend transaction (if confirmed)
    "spendBlockTime": n,                    // Block time of spend transaction (if confirmed)
    "spendBlockTimeFormatted": "...",       // Human-readable spend time (if confirmed)
    "spendConfirmed": true|false,           // Whether spend is confirmed (false if in mempool)
    "spendStatus": "...",                   // Spend status (e.g., "pending in mempool")
    "label": "...",                         // Optional label
    "counterparty": "..."                   // Optional counterparty identifier
  }
]
```

Note: Fields marked as optional or conditional may not appear in all responses depending on the swap state.

**Example:**
```bash
# List all swaps
ycash-cli listatomicswaps

# List only initiated swaps
ycash-cli listatomicswaps "initiated"

# List only claimed swaps
ycash-cli listatomicswaps "claimed"
```

### `participateswap`

Register interest in an atomic swap contract as a participant. This allows you to monitor when funds are claimed or refunded.

**Syntax:**
```
participateswap "contract" "contracttxid" vout "counterpartyaddress" ( "label" )
```

**Arguments:**
- `contract` (string, required): The HTLC contract (redeem script) in hex
- `contracttxid` (string, required): The transaction ID of the contract
- `vout` (numeric, required): The output index
- `counterpartyaddress` (string, required): Address of the counterparty (typically initiator)
- `label` (string, optional): User-defined label for the swap

**Returns:**
```json
{
  "swapId": "txid:vout",           // The swap ID
  "status": "participated",        // Initial status
  "contractTxid": "hex",           // The contract transaction ID
  "contractVout": n,               // The output index
  "recipientAddress": "address",   // Your address (if determinable from wallet)
  "initiatorAddress": "address"    // The initiator's address
}
```

**Example:**
```bash
ycash-cli participateswap "6382012088a820..." "abc123..." 0 "s1InitiatorAddressHere"
```

### `getswapsecret`

Retrieve the secret for an atomic swap if it is known.

**Syntax:**
```
getswapsecret "swapid"
```

**Arguments:**
- `swapid` (string, required): The swap ID in format "txid:vout"

**Returns:**
```json
{
  "secret": "hex",       // The secret preimage in hex
  "secretHash": "hex",   // The HASH160 of the secret
  "known": true          // Always true if this command succeeds
}
```

**Example:**
```bash
ycash-cli getswapsecret "abc123...:0"
```

## Cross-Chain Atomic Swap Protocol

Here's how to perform an atomic swap between Ycash and another cryptocurrency (e.g., Bitcoin):

### Prerequisites
- Both parties run nodes for both blockchains
- Agree on exchange rate and amounts
- **CRITICAL**: The party who generates the secret goes FIRST and uses a LONGER locktime
- Choose appropriate locktime difference (recommended: 24-48 hours between chains)

### Protocol Steps

**Scenario: Alice generates the secret and wants BTC, Bob wants YEC**

Since Alice generates the secret, she goes FIRST and uses a LONGER locktime. The locktimes are chosen so that:
- Alice's YEC locktime = Current time + 48 hours
- Bob's BTC locktime = Current time + 24 hours

This ensures Alice has time to claim after Bob's locktime expires if needed.

#### Step 1: Alice Initiates on Ycash (First, Longer Locktime)
```bash
# Alice generates secret and initiates swap on Ycash
# Uses LONGER locktime (48 hours from now)
alice$ ycash-cli initiateswap <alice_funding_address> <bob_ycash_address> 10.0 550000
{
  "contract": "6382012088a820...",
  "contractP2SH": "s3...",
  "contractTxid": "abc123...",
  "contractVout": 0,
  "secret": "1234567890abcdef...",
  "secretHash": "9876543210fedcba...",
  "refundLocktime": 550000
}
```

#### Step 2: Alice Sends Secret Hash to Bob
```bash
# Alice sends Bob the contract details and secret hash (NOT the secret)
# Bob can verify using auditswap
```

#### Step 3: Bob Audits Alice's Contract on Ycash
```bash
# Bob verifies the Ycash contract
bob$ ycash-cli auditswap "6382012088a820..." "abc123..." 0
{
  "contractValue": 10.0,
  "recipientAddress": "<bob_ycash_address>",
  "secretHash": "9876543210fedcba...",
  "refundLocktime": 550000
}
```

#### Step 4: Bob Initiates on Bitcoin (Second, Shorter Locktime)
```bash
# Bob uses the SAME secret hash to create contract on Bitcoin
# Uses SHORTER locktime (24 hours from now, ~548000 blocks equivalent)
bob$ bitcoin-cli initiateswapfromhash <bob_funding_address> <alice_btc_address> 0.1 548000 9876543210fedcba...
{
  "contract": "6382012088a820...",
  "contractP2SH": "3...",
  "contractTxid": "def456...",
  "contractVout": 0,
  "secretHash": "9876543210fedcba...",
  "refundLocktime": 548000
}
```

#### Step 5: Alice Audits Bob's Contract on Bitcoin
```bash
# Alice verifies the Bitcoin contract
alice$ bitcoin-cli auditswap "6382012088a820..." "def456..." 0
{
  "contractValue": 0.1,
  "recipientAddress": "<alice_btc_address>",
  "secretHash": "9876543210fedcba...",
  "refundLocktime": 548000
}
```

#### Step 6: Alice Claims on Bitcoin (Reveals Secret)
```bash
# Alice claims BTC by revealing the secret
# This reveals the secret to the Bitcoin blockchain
alice$ bitcoin-cli claimswap "6382012088a820..." "def456..." 0 "1234567890abcdef..."
{
  "txid": "ghi789..."
}
```

#### Step 7: Bob Extracts Secret and Claims on Ycash
```bash
# Bob watches Bitcoin blockchain, sees Alice's claim transaction
# Extracts the secret from the claim transaction
# Then claims YEC using the same secret
bob$ ycash-cli claimswap "6382012088a820..." "abc123..." 0 "1234567890abcdef..."
{
  "txid": "jkl012..."
}
```

### Refund Scenario

If Bob never initiates on Bitcoin, or if the swap fails:

```bash
# After locktime expires, Alice can refund her YEC
alice$ ycash-cli refundswap "6382012088a820..." "abc123..." 0
```

## Security Considerations

### Locktime Selection (CRITICAL FOR SECURITY)

**Golden Rule:** The party who generates the secret MUST go first and use a LONGER locktime.

- **Why:** If the secret holder goes second with a shorter locktime, they can refund after their locktime expires, then use the secret to steal the counterparty's funds
- **Locktime Relationship:** SecretHolderLocktime = OtherPartyLocktime + SafetyMargin
- **Recommended SafetyMargin:** 24-48 hours (to account for block time variance and claim propagation)
- **Example:** If Alice generates the secret:
  - Alice's YEC locktime: Current time + 48 hours
  - Bob's BTC locktime: Current time + 24 hours
  - Alice goes FIRST creating her HTLC on Ycash
  - Bob goes SECOND creating his HTLC on Bitcoin

**Attack Vector if Done Wrong:**
1. If Bob (non-secret-holder) goes first with longer locktime
2. Alice (secret holder) goes second with shorter locktime
3. Alice's shorter locktime expires first
4. Alice refunds her HTLC, getting her coins back
5. Alice then uses the secret to claim Bob's HTLC
6. Result: Alice gets both her original coins AND Bob's coins (theft!)

### Secret Generation
- Always use cryptographically secure random number generation
- Never reuse secrets across swaps
- Secret MUST be 32 bytes (256 bits)

### Amount Verification
- Always audit the counterparty's contract before proceeding
- Verify amounts match the agreed exchange rate
- Check that addresses and locktimes are correct

### Network Confirmations
- Wait for sufficient confirmations before claiming
- **Recommended**: 6+ confirmations on both chains before Alice claims on Bitcoin

## Implementation Files

### Core Implementation
- `src/script/atomicswap.h` - Header file with data structures and function declarations
- `src/script/atomicswap.cpp` - HTLC script builder and verification functions
- `src/rpc/atomicswap.cpp` - RPC command implementations

### Build System
- `src/Makefile.am` - Updated to include atomic swap source files
- `src/rpc/register.h` - Updated to register atomic swap RPC commands

### Tests
- `qa/rpc-tests/atomicswap.py` - Comprehensive test suite for atomic swap functionality

## Testing

Run the atomic swap test suite:

```bash
./qa/pull-tester/rpc-tests.py atomicswap
```

## Building

The atomic swap functionality is automatically included when building ycashd:

```bash
./zcutil/build.sh
```

## Technical Details

### Script Opcodes Used
- `OP_IF` / `OP_ELSE` / `OP_ENDIF` - Conditional execution
- `OP_HASH160` - RIPEMD160(SHA256(x))
- `OP_EQUALVERIFY` - Verify equality
- `OP_CHECKSIG` - Signature verification
- `OP_CHECKLOCKTIMEVERIFY` - Timelock verification (BIP65)
- `OP_DROP` - Remove top stack item

### Hash Function
Uses `HASH160` which is Bitcoin's standard: `RIPEMD160(SHA256(data))`

This produces a 20-byte (160-bit) hash, which is displayed as 40 hexadecimal characters.

### Cross-Chain Compatibility and Endianness

When implementing atomic swaps with other blockchains (e.g., Ethereum smart contracts):

1. **Hash Function Must Match:** The other blockchain must implement HASH160 (RIPEMD160(SHA256(data)))
   - In Solidity: `ripemd160(abi.encodePacked(sha256(abi.encodePacked(secret))))`
   - In JavaScript/Web3: Use `web3.utils.soliditySha3()` then `web3.utils.ripemd160()`

2. **Endianness Handling:**
   - Ycashd internally stores uint160 values in little-endian format
   - When displaying via `GetHex()`, bytes are reversed to big-endian (standard hex display)
   - When parsing via `SetHex()`, bytes are reversed from big-endian to little-endian storage
   - **For users:** Always provide secret hashes as big-endian hex strings (standard format)
   - Ycashd handles the conversion automatically

3. **Secret Size:** Secret must be exactly 32 bytes (256 bits) across all chains

### Transaction Structure
- Uses P2SH (Pay-to-Script-Hash) for contract deployment
- Compatible with standard Ycash transaction format
- Supports both Sapling and pre-Sapling transaction versions

### Signature Generation
- Uses Ycash's `SignatureHash` function with `consensusBranchId` parameter
- Consensus branch ID is determined by current blockchain height using `CurrentEpochBranchId()`
- This differs from standard Bitcoin signing and is critical for Zcash-based chains
- Ensures signatures are valid for the current network upgrade epoch

## Future Enhancements

Possible improvements for future versions:
- Submarine swaps (off-chain/on-chain)
- Multi-party atomic swaps
- GUI integration
- Automatic market making
- Integration with decentralized exchanges
- Support for Sapling shielded addresses (requires zkSNARK circuits)

## References

- [BIP65: CHECKLOCKTIMEVERIFY](https://github.com/bitcoin/bips/blob/master/bip-0065.mediawiki)
- [BIP199: Hashed Time-Locked Contracts](https://github.com/bitcoin/bips/blob/master/bip-0199.mediawiki)
- [Atomic Swap Research](https://en.bitcoin.it/wiki/Atomic_swap)

## License

Copyright (c) 2025 The Ycash developers
Distributed under the MIT software license.
