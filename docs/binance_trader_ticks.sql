DROP TABLE IF EXISTS quant.binance_trades_tick;
CREATE TABLE quant.binance_trades_tick (
    time_stamp Int64,
    symbol String,
    open Float64,
    high Float64,
    low Float64,
    close Float64,
    volume Float64,
    amount Float64,
    trade Int32,
    type String,
    datetime String,
    date String,
    date_stamp Int64
) ENGINE = MergeTree() 
ORDER BY (symbol, time_stamp);
