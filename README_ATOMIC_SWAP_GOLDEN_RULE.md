# Atomic Swap Golden Rule

## The One Rule to Remember

**The person who knows the secret MUST go FIRST with a LONGER locktime.**

This rule applies **regardless of**:
- Which blockchain (Ycash, Ethereum, Bitcoin, etc.)
- Which asset (YEC, USDT, BTC, etc.)
- Who initiates the swap conversation
- Who has which assets

## Why This Rule Exists

If the secret holder goes second with a shorter locktime, they can:
1. Wait for their shorter locktime to expire
2. Refund their own contract (get their funds back)
3. Still have time to claim the counterparty's contract using the secret
4. **Steal the counterparty's funds**

## The Pattern

```
1. Secret Holder → Creates HTLC FIRST (longer locktime)
2. Non-Holder   → Creates HTLC SECOND (shorter locktime)
3. Secret Holder → Claims counterparty's HTLC (reveals secret!)
4. Non-Holder   → Claims using revealed secret
```

## Why This Order Works

**Secret holder claims the counterparty's HTLC to get the funds they want:**
- This claim transaction reveals the secret on-chain
- Non-holder monitors and extracts the secret from this claim
- Non-holder then uses the secret to claim their HTLC

**Example (Alice generates secret):**
1. Alice creates YEC HTLC (24h locktime)
2. Bob creates USDT HTLC (12h locktime)
3. **Alice claims Bob's USDT** ← Secret revealed here!
4. Bob extracts secret, claims Alice's YEC

The secret holder doesn't claim their own HTLC - they claim the **counterparty's** HTLC to get what they want, which reveals the secret.

## Locktime Math

```
Secret Holder Locktime = Non-Holder Locktime + Safety Margin

Example:
Secret holder: 24 hours
Non-holder:    12 hours
Safety margin: 12 hours
```

The safety margin must be large enough for:
- Blockchain monitoring
- Secret extraction from claim transaction
- Creating and broadcasting claim transaction
- Blockchain confirmation

**Recommended safety margins:**
- Fast chains (Ethereum): 4-12 hours
- Slow chains (Bitcoin): 24-48 hours
- Cross-chain swaps: 12-24 hours

## Quick Check: Is Your Swap Secure?

Answer these questions:

1. **Who generates the secret?**
   - Answer: ___________

2. **Does that person go FIRST?**
   - ☐ Yes ✓
   - ☐ No ✗ **UNSAFE!**

3. **Does that person have the LONGER locktime?**
   - ☐ Yes ✓
   - ☐ No ✗ **UNSAFE!**

If you answered NO to either question 2 or 3, **DO NOT PROCEED** - the swap is unsafe!

## Common Mistakes

### ❌ Mistake 1: "Initiator goes first"

Wrong! The *secret holder* goes first, not the "initiator" of the conversation.

Example:
- Bob contacts Alice to trade
- Alice generates the secret
- **Alice goes first** (not Bob, even though he initiated)

### ❌ Mistake 2: "Person with more valuable asset goes first"

Wrong! The *secret holder* goes first, regardless of asset value.

Example:
- Alice has 1 BTC ($40,000)
- Bob has 40,000 USDT ($40,000)
- Alice generates the secret
- **Alice goes first** (not based on which is "worth more")

### ❌ Mistake 3: "Same locktime for both"

Wrong! Secret holder needs *longer* locktime.

Example:
- Alice and Bob both use 24 hours
- **UNSAFE!** If Alice knows the secret, Bob needs shorter locktime

### ❌ Mistake 4: "Person on slower blockchain goes first"

Wrong! The *secret holder* goes first, not based on blockchain speed.

Example:
- Alice on Bitcoin (slower)
- Bob on Ethereum (faster)
- Bob generates the secret
- **Bob goes first** (not Alice, even though Bitcoin is slower)

## What "First" Means

"Goes first" means:
- Creates their HTLC contract first
- Broadcasts their transaction first
- Locks their funds first

It does NOT mean:
- Initiates the conversation
- Proposes the trade
- Has the more valuable asset
- Is on the slower blockchain

## Implementation Checklist

Before executing a swap:

- [ ] Identify who generates the secret
- [ ] Secret holder creates their HTLC FIRST
- [ ] Secret holder uses LONGER locktime
- [ ] Non-holder verifies secret holder's HTLC
- [ ] Non-holder creates their HTLC SECOND
- [ ] Non-holder uses SHORTER locktime
- [ ] Verify locktime difference is adequate (≥12 hours for cross-chain)
- [ ] Verify secret hashes match in both HTLCs
- [ ] Set up monitoring for both chains
- [ ] Have automated claiming ready

## The Trust Model

When following the golden rule:
- ✅ No trust required
- ✅ No party can steal
- ✅ Both parties incentivized to cooperate
- ✅ Timeouts prevent fund locking
- ✅ Fully atomic (both succeed or both fail)

When violating the golden rule:
- ❌ Secret holder can steal
- ❌ Non-holder at risk
- ❌ Swap is NOT atomic
- ❌ Trust is required (trustful swap, not atomic swap!)

## Remember

**Secret holder → FIRST → LONGER locktime**

This is the ONLY secure way to do atomic swaps. Any other arrangement is unsafe.
