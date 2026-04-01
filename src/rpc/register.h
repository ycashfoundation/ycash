// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef BITCOIN_RPC_REGISTER_H
#define BITCOIN_RPC_REGISTER_H

/** These are in one header file to avoid creating tons of single-function
 * headers for everything under src/rpc/ */
class CRPCTable;

/** Register block chain RPC commands */
void RegisterBlockchainRPCCommands(CRPCTable &tableRPC);
/** Register P2P networking RPC commands */
void RegisterNetRPCCommands(CRPCTable &tableRPC);
/** Register miscellaneous RPC commands */
void RegisterMiscRPCCommands(CRPCTable &tableRPC);
/** Register mining RPC commands */
void RegisterMiningRPCCommands(CRPCTable &tableRPC);
/** Register raw transaction RPC commands */
void RegisterRawTransactionRPCCommands(CRPCTable &tableRPC);
/** Register atomic swap RPC commands */
void RegisterAtomicSwapRPCCommands(CRPCTable &tableRPC);
/** Initialize atomic swap database */
void InitAtomicSwapDatabase();
/** Shutdown atomic swap database */
void ShutdownAtomicSwapDatabase();
class CTransaction;
/** Monitor transaction for atomic swap spending */
void MonitorAtomicSwapTransaction(const CTransaction& tx);
/** Check all swaps for expiration */
void CheckAtomicSwapExpirations(int currentHeight, int64_t currentTime);
/** Handle atomic swap disconnect due to chain reorg */
void HandleAtomicSwapDisconnect(const CTransaction& tx);

static inline void RegisterAllCoreRPCCommands(CRPCTable &tableRPC)
{
    RegisterBlockchainRPCCommands(tableRPC);
    RegisterNetRPCCommands(tableRPC);
    RegisterMiscRPCCommands(tableRPC);
    RegisterMiningRPCCommands(tableRPC);
    RegisterRawTransactionRPCCommands(tableRPC);
    RegisterAtomicSwapRPCCommands(tableRPC);
}

#endif // BITCOIN_RPC_REGISTER_H
