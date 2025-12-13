#!/usr/bin/env python3
# Copyright (c) 2025 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .

#
# Test atomic swap functionality
#

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import (
    assert_equal,
    assert_raises,
    start_nodes,
    connect_nodes_bi,
    sync_blocks,
    sync_mempools
)

from decimal import Decimal
import time

class AtomicSwapTest(BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 2

    def setup_network(self, split=False):
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir)
        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        print("Mining blocks...")
        # Generate some blocks and mature coinbase outputs
        self.nodes[0].generate(101)
        self.sync_all()

        # Fund node 0 and node 1
        addr0 = self.nodes[0].getnewaddress()
        addr1 = self.nodes[1].getnewaddress()

        self.nodes[0].sendtoaddress(addr0, 10.0)
        self.nodes[0].generate(1)
        self.sync_all()

        print("Testing initiateswap...")
        self.test_initiate_swap()

        print("Testing auditswap...")
        self.test_audit_swap()

        print("Testing claimswap...")
        self.test_claim_swap()

        print("Testing refundswap...")
        self.test_refund_swap()

        print("Testing invalid parameters...")
        self.test_invalid_parameters()

        print("All atomic swap tests passed!")

    def test_initiate_swap(self):
        """Test initiating an atomic swap"""
        node = self.nodes[0]
        funding_addr = node.getnewaddress()
        recipient_addr = self.nodes[1].getnewaddress()

        # Fund the funding address
        node.sendtoaddress(funding_addr, 5.0)
        node.generate(1)
        self.sync_all()

        # Get current height for locktime
        current_height = node.getblockcount()
        locktime = current_height + 50  # 50 blocks in the future

        # Initiate swap (fundingaddress is first parameter)
        result = node.initiateswap(funding_addr, recipient_addr, 1.0, locktime)

        # Verify result structure
        assert 'contract' in result
        assert 'contractP2SH' in result
        assert 'contractTxid' in result
        assert 'contractVout' in result
        assert 'secret' in result
        assert 'secretHash' in result
        assert 'refundLocktime' in result

        # Verify values
        assert len(result['contract']) > 0
        assert len(result['secret']) == 64  # 32 bytes = 64 hex chars
        assert len(result['secretHash']) == 40  # 20 bytes = 40 hex chars
        assert result['refundLocktime'] == locktime

        # Save for later tests
        self.swap_contract = result['contract']
        self.swap_txid = result['contractTxid']
        self.swap_vout = result['contractVout']
        self.swap_secret = result['secret']
        self.swap_locktime = locktime

        # Mine a block to confirm the contract
        node.generate(1)
        self.sync_all()

        print("  ✓ Swap initiated successfully")

    def test_audit_swap(self):
        """Test auditing an atomic swap contract"""
        node = self.nodes[1]  # Use the other node to audit

        # Audit the swap
        result = node.auditswap(
            self.swap_contract,
            self.swap_txid,
            self.swap_vout
        )

        # Verify result structure
        assert 'contractP2SH' in result
        assert 'contractValue' in result
        assert 'recipientAddress' in result
        assert 'initiatorAddress' in result
        assert 'secretHash' in result
        assert 'refundLocktime' in result
        assert 'locktimeReached' in result

        # Verify values
        assert result['contractValue'] == Decimal('1.0')
        assert result['refundLocktime'] == self.swap_locktime
        assert result['locktimeReached'] == False  # Should not be reached yet

        print("  ✓ Swap audited successfully")

    def test_claim_swap(self):
        """Test claiming an atomic swap by revealing the secret"""
        node = self.nodes[1]  # Recipient claims
        claim_addr = node.getnewaddress()

        # Claim the swap
        result = node.claimswap(
            self.swap_contract,
            self.swap_txid,
            self.swap_vout,
            self.swap_secret,
            claim_addr
        )

        # Verify result structure
        assert 'txid' in result
        assert 'hex' in result

        # Save claim txid for verification
        claim_txid = result['txid']

        # Mine a block to confirm the claim
        node.generate(1)
        self.sync_all()

        # Verify the claim transaction is confirmed
        tx = node.getrawtransaction(claim_txid, 1)
        assert tx['confirmations'] >= 1

        # Verify funds were received
        balance_after = node.getbalance()
        assert balance_after > 0

        print("  ✓ Swap claimed successfully")

    def test_refund_swap(self):
        """Test refunding an atomic swap after locktime"""
        node0 = self.nodes[0]
        node1 = self.nodes[1]

        # Create a new swap for refund testing
        funding_addr = node0.getnewaddress()
        recipient_addr = node1.getnewaddress()

        # Fund the funding address
        node0.sendtoaddress(funding_addr, 5.0)
        node0.generate(1)
        self.sync_all()

        current_height = node0.getblockcount()
        locktime = current_height + 10  # Short locktime for testing

        # Initiate swap (fundingaddress is first parameter)
        result = node0.initiateswap(funding_addr, recipient_addr, 0.5, locktime)

        contract = result['contract']
        txid = result['contractTxid']
        vout = result['contractVout']

        # Mine a block to confirm
        node0.generate(1)
        self.sync_all()

        # Try to refund before locktime (should fail)
        try:
            node0.refundswap(contract, txid, vout)
            assert False, "Should have failed - locktime not reached"
        except JSONRPCException as e:
            assert "Locktime has not been reached" in e.error['message']

        # Mine blocks until locktime is reached
        blocks_to_mine = locktime - node0.getblockcount() + 1
        node0.generate(blocks_to_mine)
        self.sync_all()

        # Now refund should work
        refund_addr = node0.getnewaddress()
        result = node0.refundswap(contract, txid, vout, refund_addr)

        # Verify result structure
        assert 'txid' in result
        assert 'hex' in result

        # Mine a block to confirm the refund
        node0.generate(1)
        self.sync_all()

        # Verify the refund transaction is confirmed
        refund_txid = result['txid']
        tx = node0.getrawtransaction(refund_txid, 1)
        assert tx['confirmations'] >= 1

        print("  ✓ Swap refunded successfully")

    def test_invalid_parameters(self):
        """Test error handling for invalid parameters"""
        node = self.nodes[0]
        funding_addr = node.getnewaddress()
        recipient_addr = self.nodes[1].getnewaddress()
        current_height = node.getblockcount()

        # Fund the funding address for tests
        node.sendtoaddress(funding_addr, 5.0)
        node.generate(1)
        self.sync_all()
        current_height = node.getblockcount()

        # Test invalid amount
        try:
            node.initiateswap(funding_addr, recipient_addr, -1.0, current_height + 50)
            assert False, "Should have failed - negative amount"
        except JSONRPCException as e:
            assert "positive" in e.error['message'].lower() or "amount" in e.error['message'].lower()

        # Test invalid locktime (in the past)
        try:
            node.initiateswap(funding_addr, recipient_addr, 1.0, current_height - 10)
            assert False, "Should have failed - locktime in past"
        except JSONRPCException as e:
            assert "future" in e.error['message'].lower()

        # Test invalid locktime (too soon)
        try:
            node.initiateswap(funding_addr, recipient_addr, 1.0, current_height + 5)
            assert False, "Should have failed - locktime too soon"
        except JSONRPCException as e:
            assert "at least" in e.error['message'].lower()

        # Test invalid secret length
        try:
            node.initiateswap(funding_addr, recipient_addr, 1.0, current_height + 50, "0" * 30)
            assert False, "Should have failed - invalid secret length"
        except JSONRPCException as e:
            assert "32 bytes" in e.error['message']

        # Test invalid contract hex in auditswap
        try:
            node.auditswap("invalid_hex", "0" * 64, 0)
            assert False, "Should have failed - invalid contract"
        except JSONRPCException as e:
            assert "hex" in e.error['message'].lower()

        # Test invalid secret in claimswap
        try:
            # Create a swap first
            result = node.initiateswap(funding_addr, recipient_addr, 0.1, current_height + 50)
            node.generate(1)
            self.sync_all()

            # Try to claim with wrong secret
            wrong_secret = "0" * 64
            node.claimswap(
                result['contract'],
                result['contractTxid'],
                result['contractVout'],
                wrong_secret
            )
            assert False, "Should have failed - wrong secret"
        except JSONRPCException as e:
            assert "does not match" in e.error['message'].lower()

        print("  ✓ Invalid parameter handling verified")

if __name__ == '__main__':
    AtomicSwapTest().main()
