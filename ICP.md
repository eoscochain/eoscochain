# Inter Chain Protocol (ICP)

## ICP Overview

建立于EOSIO软件堆栈之上的ICP跨链基础设施，可应用于EOSIO兼容的同构区块链的跨链通信。ICP设计之初，就考虑怎样以一种无侵入、可安全验证、去中心化的方式来实现EOS多链并行的同构跨链网络。经过对业界最前沿的跨链技术的研究（包括BTC Relay、Cosmos、Polkadot等），并结合对EOSIO软件实现细节的差异化剖析，ICP采取了比较现实的跨链方案：
- 实现类似于轻节点的跨链基础合约，对对端区块链进行区块头跟随和验证，使用Merkle树根和路径验证跨链交易的真实性，依赖全局一致的顺序来保证跨链交易同时遵循两条链的共识。
- 实现无需信任、去中心化的跨链中继，尽最大可能地向对端传递区块头和跨链交易，并对丢失、重复、超时、伪造的跨链消息进行合适的处理。

![ICP示意图](./images/icp-architecture.png)

## ICP Relay

![ICP多中继P2P网络](./images/icp-multiple-relays.png)

## ICP Components

#### ICP Relay Plugin

- [ICP Relay Plugin](https://github.com/eoscochain/eoscochain/tree/master/plugins/icp_relay_plugin): 
- [ICP Relay API Plugin](https://github.com/eoscochain/eoscochain/tree/master/plugins/icp_relay_api_plugin):

#### ICP Contract

- [ICP Contract](https://github.com/eoscochain/eoscochain/tree/master/contracts/icp)

#### ICP Token Contract

- [ICP Token Contract](https://github.com/eoscochain/eoscochain/tree/master/contracts/icp.token)
