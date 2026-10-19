#include "order_margin.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace dynamicleverage::mt5;
namespace {
unsigned checks = 0;
void expect(bool value, const char* message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
struct State {
  int allocations = 0, alive = 0, released = 0, fail_allocation = 0;
  int assigns = 0, setters = 0, calls = 0, throw_call = 0;
  std::uint32_t assign_code = 0, setter_code = 0, baseline_code = 0, adjusted_code = 0;
  bool throw_assign = false, throw_setter = false, ignore_rate = false, drift = false;
  bool wrong_login = false, bad_digits = false, bad_free = false, nan_margin = false;
  bool retain_rate = true, nonlinear = false, inconsistent_free = false;
  double original_rate = 1.0, increment = 100, current = 150, equity = 10000, rounding_adjustment = 0;
  std::vector<double> observed_rates;
};
struct Order {
  State* state = nullptr;
  double rate = 1;
  std::uint64_t login = 1001, volume = 100000000;
  std::uint32_t type = 0;
  bool owned = false;
  void Release() { --state->alive; ++state->released; delete this; }
  std::uint64_t Login() const { return login; }
  std::uint32_t Type() const { return type; }
  std::uint64_t VolumeCurrentExt() const { return volume; }
  double RateMargin() const { return rate; }
  std::uint32_t RateMargin(double value) {
    ++state->setters;
    if (state->throw_setter) throw 42;
    if (state->retain_rate) rate = value;
    return state->setter_code;
  }
  std::uint32_t Assign(const Order* source) {
    ++state->assigns;
    if (state->throw_assign) throw std::runtime_error("Assign failure");
    rate = source->rate; login = source->login; volume = source->volume; type = source->type;
    return state->assign_code;
  }
};
struct Account {
  State* state;
  double margin = 0, free = 0;
  std::uint64_t login = 1001;
  std::uint32_t digits = 2;
  void Release() { --state->alive; ++state->released; delete this; }
  double Margin() const { return margin; }
  double MarginFree() const { return free; }
  std::uint64_t Login() const { return login; }
  std::uint32_t CurrencyDigits() const { return digits; }
};
struct Api {
  State state;
  Order source() { Order value; value.state = &state; value.rate = state.original_rate; return value; }
  bool allocate() {
    ++state.allocations;
    if (state.allocations == state.fail_allocation) return false;
    ++state.alive; return true;
  }
  Order* OrderCreate() {
    if (!allocate()) return nullptr;
    auto* value = new Order; value->state = &state; value->owned = true; return value;
  }
  Account* UserCreateAccount() { return allocate() ? new Account{&state} : nullptr; }
  std::uint32_t TradeMarginCheck(const Order* order, Account* future, Account* current) {
    ++state.calls;
    state.observed_rates.push_back(order->RateMargin());
    if (state.calls == state.throw_call) throw std::runtime_error("margin check failure");
    const auto current_margin = state.current + (state.drift && state.calls == 2 ? 50 : 0);
    const auto rate = state.ignore_rate ? state.original_rate : order->RateMargin();
    current->margin = current_margin;
    future->margin = current_margin + state.increment * rate;
    if (state.calls == 2) future->margin += state.rounding_adjustment;
    if (state.nonlinear && state.calls == 2) future->margin += 20;
    current->free = state.equity - current->margin;
    future->free = state.equity - future->margin;
    if (state.bad_free) future->free = -1;
    if (state.inconsistent_free && state.calls == 2) future->free += 10;
    if (state.nan_margin) future->margin = std::numeric_limits<double>::quiet_NaN();
    if (state.wrong_login) future->login = 9999;
    if (state.bad_digits) future->digits = current->digits = 9;
    return state.calls == 1 ? state.baseline_code : state.adjusted_code;
  }
};
OrderMarginProbeScope confirmed() { return {true, true, true, true, true, true}; }
void unchanged(const Order& value, double rate) {
  expect(value.rate == rate && value.login == 1001 && value.volume == 100000000 && !value.owned,
         "original order and original conversion rate are untouched");
}
void rejected(Api& api, const char* message) {
  auto original = api.source();
  auto result = prepare_order_margin_probe(api, original, 1000, 500, confirmed());
  expect(result.status != OrderMarginProbeStatus::validated && !result.candidate, message);
  expect(api.state.alive == 0, "all rejected candidate/account objects released");
  unchanged(original, api.state.original_rate);
}
void tests() {
  Api api;
  api.state.original_rate = 1.25;
  auto original = api.source();
  {
    auto result = prepare_order_margin_probe(api, original, 1000, 500, confirmed());
    expect(result.status == OrderMarginProbeStatus::validated && result.candidate,
           "SDK confirms adjusted copy for a new additive position");
    expect(result.original_rate == 1.25 && result.candidate_rate == 2.5 && result.multiplier == 2,
           "conversion rate is scaled from its original value, never assumed to be one");
    expect(result.baseline_future.margin == 275 && result.adjusted_future.margin == 400 &&
           result.expected_margin == 400 && result.adjusted_future.free_margin == 9600,
           "account margin increases only for new order contribution");
    expect(result.baseline_code == 0 && result.adjusted_code == 0 && result.assign_code == 0 && result.rate_code == 0,
           "diagnostics retain both margin-check and preparation return codes");
    expect(api.state.observed_rates == std::vector<double>({1.25, 2.5}) && api.state.setters == 1,
           "original is checked before a single adjustment of its copy");
    expect(api.state.alive == 1 && api.state.released == 4,
           "only validated candidate ownership survives; all account outputs released");
    unchanged(original, 1.25);
  }
  expect(api.state.alive == 0 && api.state.released == 5, "caller-owned candidate released automatically");

  Api rounded; rounded.state.rounding_adjustment = .01;
  auto rounded_order = rounded.source();
  auto rounding_result = prepare_order_margin_probe(rounded, rounded_order, 1000, 200, confirmed());
  expect(rounding_result.status == OrderMarginProbeStatus::validated && rounding_result.candidate &&
         rounding_result.candidate_rate == 5 && std::abs(rounding_result.expected_margin - 650) < 1e-9,
         "fivefold multiplier accepts an explainable account currency rounding difference");
  rounding_result.candidate.reset();
  expect(rounded.state.alive == 0, "rounded candidate also releases all resources");

  Api unknown;
  auto raw = unknown.source();
  auto evidence = prepare_order_margin_probe(unknown, raw, 1000, 500, {});
  expect(evidence.status == OrderMarginProbeStatus::unsupported && !evidence.candidate && !evidence.scope_confirmed &&
         unknown.state.calls == 2 && evidence.adjusted_future.margin == 350 && unknown.state.alive == 0,
         "unknown configuration collects read-only evidence without approving a candidate");
  unchanged(raw, 1);

  Api ignored; ignored.state.ignore_rate = true; rejected(ignored, "SDK ignoring RateMargin cannot validate");
  Api nonlinear; nonlinear.state.nonlinear = true; rejected(nonlinear, "non-additive margin cannot validate");
  Api hedge; hedge.state.increment = 0; rejected(hedge, "hedged zero delta cannot validate");
  Api too_small; too_small.state.increment = .02; rejected(too_small, "rounding cannot mask an unobservable change");
  Api drift; drift.state.drift = true; rejected(drift, "changed account state between checks cannot validate");
  Api funds; funds.state.adjusted_code = 10019; rejected(funds, "insufficient adjusted funds discard candidate");
  expect(funds.state.calls == 2, "adjusted rejection is diagnosed after baseline check");
  Api baseline; baseline.state.baseline_code = 10019; rejected(baseline, "baseline failure cannot validate");
  expect(baseline.state.calls == 1 && baseline.state.setters == 0, "failed baseline stops before modifying candidate rate");
  Api bad_free; bad_free.state.bad_free = true; rejected(bad_free, "negative free margin rejects despite SDK success");
  Api inconsistent; inconsistent.state.inconsistent_free = true; rejected(inconsistent, "inconsistent margin/free margin rejected");
  Api wrong; wrong.state.wrong_login = true; rejected(wrong, "foreign account output rejected");
  Api digits; digits.state.bad_digits = true; rejected(digits, "unsupported account precision rejected");
  Api nan; nan.state.nan_margin = true; rejected(nan, "nonfinite SDK margin rejected");
  Api assign; assign.state.assign_code = 7; rejected(assign, "Assign failure releases all SDK objects");
  Api setter; setter.state.setter_code = 7; rejected(setter, "RateMargin failure releases mutated copy only");
  Api setter_ignored; setter_ignored.state.retain_rate = false; rejected(setter_ignored, "ignored setter cannot validate");
  Api throw_assign; throw_assign.state.throw_assign = true; rejected(throw_assign, "Assign exception is contained");
  Api throw_setter; throw_setter.state.throw_setter = true; rejected(throw_setter, "unknown setter exception is contained");
  for (int stage = 1; stage <= 5; ++stage) {
    Api failure; failure.state.fail_allocation = stage;
    rejected(failure, "every SDK allocation failure is handled without leaks");
  }
  for (int stage = 1; stage <= 2; ++stage) {
    Api failure; failure.state.throw_call = stage;
    rejected(failure, "either margin-check exception releases all SDK outputs");
  }
  for (auto target : {0u, 1000u, 2000u}) {
    Api invalid; auto source = invalid.source();
    auto result = prepare_order_margin_probe(invalid, source, 1000, target, confirmed());
    expect(result.status == OrderMarginProbeStatus::unsupported && invalid.state.allocations == 0,
           "probe does not silently accept zero, unchanged or increased leverage");
  }
  for (double rate : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::max()}) {
    Api invalid; invalid.state.original_rate = rate; auto source = invalid.source();
    auto result = prepare_order_margin_probe(invalid, source, 1000, 200, confirmed());
    expect(result.status == OrderMarginProbeStatus::unsupported && invalid.state.allocations == 0,
           "invalid or overflowing rate rejected before SDK allocation");
  }
  Api pending; auto pending_order = pending.source(); pending_order.type = 2;
  auto pending_result = prepare_order_margin_probe(pending, pending_order, 1000, 500, confirmed());
  expect(pending_result.status == OrderMarginProbeStatus::unsupported && pending.state.calls == 0,
         "pending activation requires its separate validation flow");
}
} // namespace
int main() {
  try { tests(); std::cout << "MT5 order margin probe: " << checks << " checks passed\n"; return 0; }
  catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
