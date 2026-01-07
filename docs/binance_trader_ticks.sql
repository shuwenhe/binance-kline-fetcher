CREATE TABLE quant.binance_trades_tick
(
    `inst_id` String,
    `timestamp` UInt64,
    `trade_id` String,
    `price` Float64,
    `size` Float64,
    `side` String
)
ENGINE = MergeTree
ORDER BY (inst_id,
 timestamp)
SETTINGS index_granularity = 8192;
