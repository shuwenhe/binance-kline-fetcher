#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <ctime>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <functional>

// ClickHouse Headers
#include <clickhouse/client.h>
#include <clickhouse/columns/numeric.h>
#include <clickhouse/columns/string.h>

using json = nlohmann::json;
using namespace clickhouse;

// ---------------- KLine Data Structure ----------------
struct KLine {
    long long time_stamp;   
    std::string symbol;     
    double open;
    double high;
    double low;
    double close;
    double volume;
    double amount;          
    int trade;              
    std::string type;       
    std::string datetime;   
    std::string date;       
    long long date_stamp;   
};

// ---------------- HTTP Request ----------------
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string http_get_with_retry(const std::string& url, int max_retry = 3, int timeout = 10) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;
    int attempt = 0;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (!curl) return "";

    while (attempt < max_retry) {
        readBuffer.clear();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
        // Binance requires a User-Agent often to avoid 403s
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        res = curl_easy_perform(curl);
        if (res == CURLE_OK) break;

        attempt++;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::cout << "Retry #" << attempt << " for " << url << " Error: " << curl_easy_strerror(res) << std::endl;
    }

    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return readBuffer;
}

// ---------------- BinanceKLineFetcher ----------------
class BinanceKLineFetcher {
public:
    BinanceKLineFetcher() {
        base_url = "https://api.binance.com/api/v3/klines"; // Updated to v3
        freq_shifting = {
            {"1m", 60 * 500},
            {"5m", 300 * 500},
            {"1h", 3600 * 500},
            {"1d", 86400 * 500}
        };
        freq_map = {
            {"1m", "1min"}, {"5m", "5min"}, {"1h", "60min"}, {"1d", "day"}
        };
    }

    std::vector<KLine> fetch_klines(const std::string& symbol, long long start_time, long long end_time,
                                    const std::string& freq,
                                    std::function<void(const std::vector<KLine>&)> callback = nullptr)
    {
        std::vector<KLine> all_klines;
        long long current_start = start_time;

        while (current_start < end_time) {
            std::string url = base_url + "?symbol=" + symbol + "&interval=" + freq +
                              "&startTime=" + std::to_string(current_start * 1000) +
                              "&limit=500";

            std::string resp = http_get_with_retry(url);
            if (resp.empty()) break;

            auto klines = parse_klines(symbol, resp, freq);
            if (klines.empty()) break;

            if (callback) callback(klines);
            all_klines.insert(all_klines.end(), klines.begin(), klines.end());

            // Move pointer forward based on the last timestamp received
            current_start = klines.back().time_stamp + 1;
            
            // Avoid hitting rate limits
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return all_klines;
    }

private:
    std::string base_url;
    std::map<std::string, long long> freq_shifting;
    std::map<std::string, std::string> freq_map;

    std::vector<KLine> parse_klines(const std::string& symbol, const std::string& json_str, const std::string& freq) {
        std::vector<KLine> klines;
        try {
            auto j = json::parse(json_str);
            for (auto& item : j) {
                KLine k;
                k.symbol = "BINANCE." + symbol;
                k.time_stamp = item[0].get<long long>() / 1000;
                k.open = std::stod(item[1].get<std::string>());
                k.high = std::stod(item[2].get<std::string>());
                k.low = std::stod(item[3].get<std::string>());
                k.close = std::stod(item[4].get<std::string>());
                k.volume = std::stod(item[5].get<std::string>());
                k.amount = std::stod(item[7].get<std::string>());
                k.trade = item[8].get<int>();
                k.type = freq_map[freq];

                std::time_t t = k.time_stamp;
                char buf[20];
                struct tm *timeinfo = std::gmtime(&t); // Using GMT for consistency
                strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", timeinfo);
                k.datetime = buf;
                strftime(buf, sizeof(buf), "%Y-%m-%d", timeinfo);
                k.date = buf;
                k.date_stamp = k.time_stamp;

                klines.push_back(k);
            }
        } catch (...) { return {}; }
        return klines;
    }
};

// ---------------- ClickHouse Storage ----------------
class ClickHouseStorage {
public:
    ClickHouseStorage(const std::string& host="localhost", const int port=9000, const std::string& db="default") {
        ClientOptions options;
        options.SetHost(host).SetPort(port).SetDefaultDatabase(db);
        client = std::make_unique<Client>(options);
    }

    void insert_klines(const std::vector<KLine>& klines) {
        if (klines.empty()) return;

        Block block;

        auto col_time_stamp = std::make_shared<ColumnInt64>();
        auto col_symbol     = std::make_shared<ColumnString>();
        auto col_open       = std::make_shared<ColumnFloat64>();
        auto col_high       = std::make_shared<ColumnFloat64>();
        auto col_low        = std::make_shared<ColumnFloat64>();
        auto col_close      = std::make_shared<ColumnFloat64>();
        auto col_volume     = std::make_shared<ColumnFloat64>();
        auto col_amount     = std::make_shared<ColumnFloat64>();
        auto col_trade      = std::make_shared<ColumnInt32>();
        auto col_type       = std::make_shared<ColumnString>();
        auto col_datetime   = std::make_shared<ColumnString>();
        auto col_date       = std::make_shared<ColumnString>();
        auto col_date_stamp = std::make_shared<ColumnInt64>();

        for (const auto& k : klines) {
            col_time_stamp->Append(k.time_stamp);
            col_symbol->Append(k.symbol);
            col_open->Append(k.open);
            col_high->Append(k.high);
            col_low->Append(k.low);
            col_close->Append(k.close);
            col_volume->Append(k.volume);
            col_amount->Append(k.amount);
            col_trade->Append(k.trade);
            col_type->Append(k.type);
            col_datetime->Append(k.datetime);
            col_date->Append(k.date);
            col_date_stamp->Append(k.date_stamp);
        }

        block.AppendColumn("time_stamp", col_time_stamp);
        block.AppendColumn("symbol",     col_symbol);
        block.AppendColumn("open",       col_open);
        block.AppendColumn("high",       col_high);
        block.AppendColumn("low",        col_low);
        block.AppendColumn("close",      col_close);
        block.AppendColumn("volume",     col_volume);
        block.AppendColumn("amount",     col_amount);
        block.AppendColumn("trade",      col_trade);
        block.AppendColumn("type",       col_type);
        block.AppendColumn("datetime",   col_datetime);
        block.AppendColumn("date",       col_date);
        block.AppendColumn("date_stamp", col_date_stamp);

        client->Insert("binance_kline", block);
    }

private:
    std::unique_ptr<Client> client;
};

// ---------------- Main ----------------
int main() {
    BinanceKLineFetcher fetcher;
    ClickHouseStorage storage("127.0.0.1", 9000, "quant");

    // Example range: Jan 1 2024 to Jan 2 2024
    long long start_time = 1704067200; 
    long long end_time   = 1704153600;

    fetcher.fetch_klines("ETHBTC", start_time, end_time, "1h",
        [&](const std::vector<KLine>& batch){
            std::cout << "Fetched batch of " << batch.size() << " KLines, inserting..." << std::endl;
            storage.insert_klines(batch);
        }
    );

    std::cout << "Data collection complete." << std::endl;
    return 0;
}
