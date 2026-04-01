# Cross-Chain Atomic Swap Guide: YEC ↔ USDT

This guide explains how to perform cross-chain atomic swaps between Ycash (YEC) and USDT (Ethereum).

## Overview

**Scenario:** Alice has YEC and wants USDT. Bob has USDT and wants YEC.

**CRITICAL SECURITY PRINCIPLE:** The person who generates the secret goes **FIRST** and uses a **LONGER** locktime.

**Why:** If the secret holder goes second, they can refund their contract after the shorter locktime expires, then steal the counterparty's funds using the secret.

## Complete Swap Flow

### Step 1: Alice Generates Secret and Hash

Alice generates a 32-byte secret and calculates its HASH160:

```bash
# Generate 32-byte random secret
SECRET=$(openssl rand -hex 32)
echo "Secret: $SECRET"

# Calculate HASH160 (SHA256 then RIPEMD160)
SECRET_HASH=$(echo -n $SECRET | xxd -r -p | sha256sum | xxd -r -p | openssl ripemd160 | awk '{print $2}')
echo "Secret Hash: $SECRET_HASH"
```

**Output:**
```
Secret: 1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef (32 bytes)
Secret Hash: a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0 (20 bytes, big-endian)
```

**IMPORTANT:** This hash is in **big-endian** format (human-readable). Both Ycash RPCs (`initiateswapfromhash` and Ethereum smart contracts) expect this format.

**Alice shares with Bob:**
- ✅ Secret hash: `a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0`
- ✅ Her Ethereum address for receiving USDT
- ❌ **NOT the secret itself!**

### Step 2: Alice Creates YEC HTLC First (24 hour locktime)

**IMPORTANT:** Alice goes **FIRST** because she knows the secret. She uses a **LONGER** locktime (24h):

```bash
# Alice creates YEC HTLC with her secret and LONGER locktime
ycash-cli initiateswap \
    "y1AliceFundingAddress" \
    "y1BobRecipientAddress" \
    100 \
    $(($(date +%s) + 86400)) \
    "$SECRET"
```

**Output:**
```json
{
  "contract": "6382012088a820...",
  "contractP2SH": "s1ABC...",
  "contractTxid": "abc123...",
  "contractVout": 0,
  "secret": "1234567890abcdef...",
  "secretHash": "a1b2c3d4e5f6...",
  "refundLocktime": 1735129600
}
```

**Alice shares with Bob:**
- Contract hex (from `contract` field)
- Transaction ID
- Vout index

### Step 3: Bob Verifies Alice's YEC Contract

```bash
ycash-cli auditswap \
    "6382012088a820..." \
    "abc123..." \
    0
```

Bob verifies:
- ✅ Amount: 100 YEC
- ✅ Secret hash matches his expected hash
- ✅ Recipient is his YEC address
- ✅ Locktime is reasonable (24 hours)

### Step 4: Bob Creates USDT HTLC Second (12 hour locktime)

**CRITICAL:** Bob goes **SECOND** with a **SHORTER** locktime (12h). This MUST be shorter than Alice's 24h:

```javascript
// Bob initiates USDT swap on Ethereum with SHORTER locktime
const usdtSwap = new AtomicSwapUSDT('0xContractAddress');

const result = await usdtSwap.initiateSwap(
    '0xAliceEthereumAddress',     // Alice's ETH address
    50,                            // 50 USDT
    '0xa1b2c3d4e5f6...',          // Alice's secret hash (20 bytes)
    12                             // 12 hour locktime (SHORTER than Alice's 24h!)
);

console.log('Swap ID:', result.swapId);
console.log('Tx Hash:', result.txHash);
```

**Bob shares with Alice:**
- Transaction hash on Etherscan
- Swap ID
- Smart contract address

### Step 5: Alice Verifies Bob's USDT Contract

Alice checks the Ethereum contract:

```javascript
const status = await usdtSwap.getSwapStatus(swapId);
console.log(status);
```

Verify:
- ✅ Amount: 50 USDT
- ✅ Secret hash matches her hash
- ✅ Recipient is her Ethereum address
- ✅ **CRITICAL:** Locktime is 12 hours (SHORTER than her 24h YEC locktime!)

### Step 6: Alice Claims USDT (Reveals Secret)

Alice claims Bob's USDT, which reveals the secret on Ethereum:

```javascript
await usdtSwap.claimSwap(
    swapId,
    '0x1234567890abcdef...'  // Alice's secret (32 bytes)
);
```

**The secret is now public on Ethereum blockchain!**

### Step 7: Bob Monitors Ethereum and Claims YEC

Bob monitors Ethereum for Alice's claim transaction, extracts the secret, and claims YEC:

```bash
# Bob extracts secret from Ethereum (Alice's claim revealed it)
# This would be done via web3.js or similar

# Bob claims Alice's YEC using the revealed secret
ycash-cli claimswap \
    "abc123...:0" \
    "y1BobDestinationAddress" \
    "$EXTRACTED_SECRET"
```

**✅ Swap Complete!**
- Alice has 50 USDT
- Bob has 100 YEC

## Alternative Flow: Bob Generates Secret

**If Bob wants to generate the secret instead, the roles completely reverse!**

### Important: Bob Now Goes FIRST (Because He Knows Secret)

**New roles:**
- **Bob (knows secret):** Goes FIRST, uses LONGER locktime (24h)
- **Alice (doesn't know secret):** Goes SECOND, uses SHORTER locktime (12h)

### Step 1: Bob Generates Secret and Hash

```bash
# Bob generates secret
SECRET=$(openssl rand -hex 32)
SECRET_HASH=$(echo -n $SECRET | xxd -r -p | sha256sum | xxd -r -p | openssl ripemd160 | awk '{print $2}')
```

Bob shares secret hash with Alice (NOT the secret!)

### Step 2: Bob Creates USDT HTLC First (24h - LONGER)

**Bob goes FIRST because he knows the secret:**

```javascript
// Bob creates USDT contract with LONGER locktime
await usdtSwap.initiateSwap(
    aliceEthAddress,
    50,                      // 50 USDT
    '0x' + SECRET_HASH,      // Bob's secret hash
    24                       // 24 hours - LONGER!
);
```

### Step 3: Alice Verifies and Creates YEC HTLC Second (12h - SHORTER)

**Alice goes SECOND with SHORTER locktime using `initiateswapfromhash`:**

```bash
# Alice creates YEC HTLC from Bob's secret hash
ycash-cli initiateswapfromhash \
    "y1AliceFundingAddress" \
    "y1BobRecipientAddress" \
    100 \
    $(($(date +%s) + 43200)) \
    "$SECRET_HASH"
```

Note: 43200 seconds = 12 hours (SHORTER than Bob's 24h)

### Step 4: Bob Claims YEC (Reveals Secret)

```bash
# Bob claims Alice's YEC (reveals secret)
ycash-cli claimswap \
    "swap_id" \
    "y1BobDestination..." \
    "$SECRET"
```

Secret is now public on Ycash blockchain!

### Step 5: Alice Monitors and Claims USDT

Alice monitors Ycash, extracts secret, claims USDT:

```javascript
// Alice extracts secret from Ycash claim
// Then claims Bob's USDT
await usdtSwap.claimSwap(swapId, extractedSecret);
```

**✅ Swap Complete!**
- Bob has 100 YEC
- Alice has 50 USDT

## Summary: Who Goes First?

| Scenario                   | Goes First | Longer Locktime   | Goes Second | Shorter Locktime |
|----------------------------|------------|-------------------|-------------|------------------|
| **Alice generates secret** | Alice (YEC)|       24h         | Bob (USDT)  |       12h        |
| **Bob generates secret**   | Bob (USDT) |       24h         | Alice (YEC) |       12h        |

**Golden Rule:** Secret holder ALWAYS goes first with longer locktime, regardless of which chain!

## RPC Commands Summary

### For Secret Generators (Know Secret)

```bash
# Create HTLC with secret
ycash-cli initiateswap \
    "fundingaddress" \
    "recipientaddress" \
    amount \
    locktime \
    "secret"
```

### For Participants (Only Know Hash)

```bash
# Create HTLC from secret hash
ycash-cli initiateswapfromhash \
    "fundingaddress" \
    "recipientaddress" \
    amount \
    locktime \
    "secrethash"
```

### Query Secret After Claim

```bash
# Extract secret from claimed swap
ycash-cli getswapsecret "swapid"
```

### List All Swaps

```bash
# View all swaps (includes contract hex)
ycash-cli listatomicswaps
```

## Security Rules

### 1. Locktime Ordering (CRITICAL!)

```
Secret Holder's Locktime (24h) > Other Party's Locktime (12h)
```

**The secret holder MUST have the LONGER locktime!**

**Why:** If the secret holder has a shorter locktime:
1. Secret holder creates their HTLC (12h locktime)
2. Other party creates their HTLC (24h locktime)
3. Secret holder waits 12h, refunds their HTLC
4. Secret holder still has 12h to claim other party's HTLC with the secret
5. **Other party loses everything!**

### 2. Who Goes First

**The secret holder goes FIRST with LONGER locktime.**

In our example:
- **Alice (knows secret):** Goes FIRST, uses 24h locktime
- **Bob (doesn't know secret):** Goes SECOND, uses 12h locktime

This is counterintuitive but essential for security!

### 3. Secret Hash Must Match

Both HTLCs must use the **exact same secret hash**:

```
Bob's Ethereum HTLC:  HASH160(secret) = 0xa1b2c3d4...
Alice's Ycash HTLC:   HASH160(secret) = 0xa1b2c3d4...  ← SAME!
```

### 4. Verification Required

**Always verify the counterparty's contract before creating yours:**

Ycash side:
```bash
ycash-cli auditswap "contract" "txid" vout
```

Ethereum side:
```javascript
await usdtSwap.getSwapStatus(swapId);
```

## Failure Scenarios

### Alice Doesn't Create Her HTLC (Step 2)

**Result:** Bob doesn't create his USDT contract. No loss for anyone.

### Bob Doesn't Create His HTLC (Step 4)

**Result:** Alice refunds her YEC after 24 hours. Alice gets her funds back, no loss.

### Alice Doesn't Claim USDT (Step 6)

**Result:**
- Bob refunds his USDT after 12h
- Alice refunds her YEC after 24h
- Both get funds back, no loss

### Bob Doesn't Claim YEC (Step 7)

**Problem:** Alice revealed the secret when claiming USDT at Step 6.

**Outcome:**
- Bob has 12 hours (from Alice's claim at ~2h until Alice's refund at 24h) to claim YEC
- If Bob misses this window, Alice can refund her YEC after 24h
- **Bob loses 50 USDT** (Alice has USDT + YEC)
- This is Bob's responsibility - he had plenty of time!

**Solution:** Run monitoring:
```bash
# Monitor for claims
ycash-cli listatomicswaps "claimed"
```

Or use automatic monitoring in JavaScript/Python.

## Hash Calculation Reference

### Calculate HASH160 in Bash

```bash
SECRET="1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"
SECRET_HASH=$(echo -n $SECRET | xxd -r -p | sha256sum | xxd -r -p | openssl ripemd160 | awk '{print $2}')
echo $SECRET_HASH
```

### Calculate HASH160 in JavaScript

```javascript
const crypto = require('crypto');

function hash160(data) {
    const buffer = Buffer.from(data, 'hex');
    const sha256Hash = crypto.createHash('sha256').update(buffer).digest();
    const ripemd160Hash = crypto.createHash('ripemd160').update(sha256Hash).digest();
    return ripemd160Hash.toString('hex');
}

const secret = '1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef';
const secretHash = hash160(secret);
console.log('Secret Hash:', secretHash);
```

### Verify Hash in Ycash

```bash
# After creating swap, verify hash matches
ycash-cli listatomicswaps | jq '.[] | select(.contractTxid=="abc123...") | .secretHash'
```

## Troubleshooting

### "Secret hash must be 20 bytes"

You provided 32 bytes (SHA256). Use HASH160 instead:
- ❌ SHA256: 32 bytes
- ✅ HASH160: 20 bytes (RIPEMD160(SHA256(data)))

### "Secret does not match hash"

The secret used in claim doesn't match the hash in the contract. Verify:

```bash
# Check what hash is in contract
ycash-cli auditswap "contract" "txid" vout | jq '.secretHash'

# Check what secret you're using
echo -n $SECRET | xxd -r -p | sha256sum | xxd -r -p | openssl ripemd160 | awk '{print $2}'
```

### "Locktime not yet passed"

You're trying to refund before the locktime. Wait until:

```bash
# Check current time vs locktime
ycash-cli listatomicswaps | jq '.[] | {locktime, timeUntilExpiry}'
```
