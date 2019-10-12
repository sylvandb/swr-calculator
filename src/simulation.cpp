#include "simulation.hpp"

#include <algorithm>
#include <iostream>
#include <numeric>

namespace {

size_t simulations = 0;

// In percent
constexpr const float monthly_rebalancing_cost   = 0.005;
constexpr const float yearly_rebalancing_cost    = 0.01;
constexpr const float threshold_rebalancing_cost = 0.01;

} // end of anonymous namespace

swr::Rebalancing swr::parse_rebalance(const std::string& str) {
    if (str == "none") {
        return Rebalancing::NONE;
    } else if (str == "monthly") {
        return Rebalancing::MONTHLY;
    } else if (str == "yearly") {
        return Rebalancing::YEARLY;
    } else {
        return Rebalancing::THRESHOLD;
    }
}

std::ostream & swr::operator<<(std::ostream& out, const Rebalancing & rebalance){
    switch (rebalance) {
        case Rebalancing::NONE:
            return out << "none";
        case Rebalancing::MONTHLY:
            return out << "monthly";
        case Rebalancing::YEARLY:
            return out << "yearly";
        case Rebalancing::THRESHOLD:
            return out << "threshold";
    }

    return out << "Unknown rebalancing";
}

swr::results swr::simulation(const std::vector<swr::allocation>& portfolio, const std::vector<swr::data>& inflation_data, const std::vector<std::vector<swr::data>>& values, size_t years, float wr, size_t start_year, size_t end_year, bool monthly_wr, Rebalancing rebalance, float threshold) {
    const size_t number_of_assets = portfolio.size();
    const float start_value       = 1000.0f;
    const size_t months           = years * 12;

    swr::results res;

    std::vector<float> terminal_values;
    std::vector<std::vector<swr::data>::const_iterator> returns(number_of_assets);

    for (size_t current_year = start_year; current_year <= end_year - years; ++current_year) {
        for (size_t current_month = 1; current_month <= 12; ++current_month) {
            size_t end_year  = current_year + (current_month - 1 + months - 1) / 12;
            size_t end_month = 1 + ((current_month - 1) + (months - 1) % 12) % 12;

            // The amount of money withdrawn per year
            float withdrawal = start_value * wr / 100.0f;

            std::vector<float> current_values(number_of_assets);

            for (size_t i = 0; i < number_of_assets; ++i) {
                current_values[i] = start_value * (portfolio[i].allocation / 100.0f);
                returns[i]        = swr::get_start(values[i], current_year, (current_month % 12) + 1);
            }

            auto inflation = swr::get_start(inflation_data, current_year, (current_month % 12) + 1);

            for (size_t y = current_year; y <= end_year; ++y) {
                for (size_t m = (y == current_year ? current_month : 1); m <= (y == end_year ? end_month : 12); ++m) {
                    // Adjust the portfolio with the returns
                    for (size_t i = 0; i < number_of_assets; ++i) {
                        current_values[i] *= returns[i]->value;
                        ++returns[i];
                    }

                    // Monthly Rebalance if necessary
                    if (rebalance == Rebalancing::MONTHLY) {
                        // Pay the fees
                        for (size_t i = 0; i < number_of_assets; ++i) {
                            current_values[i] *= 1.0f - monthly_rebalancing_cost / 100.0f;
                        }

                        auto total_value = std::accumulate(current_values.begin(), current_values.end(), 0.0f);

                        for (size_t i = 0; i < number_of_assets; ++i) {
                            current_values[i] = total_value * (portfolio[i].allocation / 100.0f);
                        }
                    }

                    // Threshold Rebalance if necessary
                    if (rebalance == Rebalancing::THRESHOLD) {
                        auto total_value = std::accumulate(current_values.begin(), current_values.end(), 0.0f);

                        bool rebalance = false;
                        for (size_t i = 0; i < number_of_assets; ++i) {
                            if (std::abs((portfolio[i].allocation / 100.0f) - current_values[i] / total_value) >= threshold) {
                                rebalance = true;
                                break;
                            }
                        }

                        if (rebalance) {
                            // Pay the fees
                            for (size_t i = 0; i < number_of_assets; ++i) {
                                current_values[i] *= 1.0f - threshold_rebalancing_cost / 100.0f;
                            }

                            for (size_t i = 0; i < number_of_assets; ++i) {
                                current_values[i] = total_value * (portfolio[i].allocation / 100.0f);
                            }
                        }
                    }

                    // Adjust the withdrawal for inflation
                    withdrawal *= inflation->value;
                    ++inflation;

                    // Withdraw money from the portfolio
                    if (monthly_wr) {
                        auto total_value = std::accumulate(current_values.begin(), current_values.end(), 0.0f);

                        for (auto& value : current_values) {
                            value = std::max(0.0f, value - (value / total_value) * (withdrawal / 12.0f));
                        }
                    }
                }

                // Yearly Rebalance if necessary
                if (rebalance == Rebalancing::YEARLY) {
                    // Pay the fees
                    for (size_t i = 0; i < number_of_assets; ++i) {
                        current_values[i] *= 1.0f - yearly_rebalancing_cost / 100.0f;
                    }

                    auto total_value = std::accumulate(current_values.begin(), current_values.end(), 0.0f);

                    for (size_t i = 0; i < number_of_assets; ++i) {
                        current_values[i] = total_value * (portfolio[i].allocation / 100.0f);
                    }
                }

                // Full yearly withdrawal
                if (!monthly_wr) {
                    auto total_value = std::accumulate(current_values.begin(), current_values.end(), 0.0f);

                    for (auto& value : current_values) {
                        value = std::max(0.0f, value - (value / total_value) * withdrawal);
                    }
                }
            }

            auto final_value = std::accumulate(current_values.begin(), current_values.end(), 0.0f);

            if (final_value > 0.0f) {
                ++res.successes;
            } else {
                ++res.failures;
            }

            terminal_values.push_back(final_value);
        }
    }

    res.success_rate = 100 * (res.successes / float(res.successes + res.failures));
    res.compute_terminal_values(terminal_values);

    simulations += terminal_values.size();

    return res;
}

void swr::results::compute_terminal_values(std::vector<float> & terminal_values) {
    std::sort(terminal_values.begin(), terminal_values.end());

    tv_median  = terminal_values[terminal_values.size() / 2 + 1];
    tv_minimum = terminal_values.front();
    tv_maximum = terminal_values.back();
    tv_average = std::accumulate(terminal_values.begin(), terminal_values.end(), 0.0f) / terminal_values.size();
}

size_t swr::simulations_ran() {
    return simulations;
}