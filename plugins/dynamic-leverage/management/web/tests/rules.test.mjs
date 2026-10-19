import test from 'node:test';
import assert from 'node:assert/strict';
import { accountNumber, formatLots, lotsUnits, selectorTokens, validateConfig, validateRule } from '../rules.mjs';

const rule = () => ({ id: 'r1', name: '标准规则', enabled: true, scope: 'group', selector: '!demo*,*', tiers: [{ above_lots: '0', leverage: 1000 }, { above_lots: '2', leverage: 500 }, { above_lots: '5', leverage: 200 }] });
test('MT5 identifiers preserve all 64 bits and canonicalize zeros without Number conversion', () => {
  assert.equal(accountNumber('18446744073709551615', 'mt5'), '18446744073709551615');
  assert.equal(accountNumber('009007199254740993', 'mt5'), '9007199254740993');
  assert.throws(() => accountNumber('18446744073709551616', 'mt5'));
  assert.throws(() => accountNumber('2147483648', 'mt4'));
  assert.throws(() => accountNumber('0', 'mt5'));
  assert.throws(() => accountNumber('1e3', 'mt5'));
  assert.equal(accountNumber('0'.repeat(254) + '1', 'mt5'), '1');
  assert.throws(() => accountNumber('0'.repeat(255) + '1', 'mt5'));
});
test('lot values use exact 1e-8 units including uint64 boundary', () => {
  assert.equal(lotsUnits('2.00000001'), 200000001n);
  assert.equal(lotsUnits('184467440737.09551615'), 18446744073709551615n);
  assert.equal(formatLots(lotsUnits('0002.50000000')), '2.5');
  for (const invalid of ['184467440737.09551616', '0.000000001', '-1', '1e2', '.5', '2.', '']) assert.throws(() => lotsUnits(invalid));
});
test('selectors reject ambiguous empty tokens and preserve case and path separators', () => {
  assert.deepEqual(selectorTokens('!ABC,real\\*, *', 'group', 'mt5'), ['!ABC', 'real\\*', '*']);
  assert.deepEqual(selectorTokens('!ABC', 'group', 'mt5'), ['!ABC']);
  for (const selector of ['', '*,,real*', '!', '?', '!!ABC', 'ABC!', '*，demo*\n', 'ABC\u0085']) assert.throws(() => selectorTokens(selector, 'group', 'mt5'));
  assert.throws(() => selectorTokens('10001,010001', 'account', 'mt5'));
  assert.deepEqual(selectorTokens('9007199254740993,18446744073709551615', 'account', 'mt5'), ['9007199254740993', '18446744073709551615']);
});
test('tiers require increasing exact thresholds and decreasing leverage', () => {
  const valid = rule(); valid.tiers[1].above_lots = '2.00000001'; assert.equal(validateRule(valid, 'mt4').tiers[1].above_lots, '2.00000001');
  for (const mutation of [r => r.tiers[0].above_lots = '1', r => r.tiers[2].above_lots = '2', r => r.tiers[2].leverage = 500, r => r.tiers[1].leverage = '', r => r.tiers[1].leverage = '1.5']) {
    const invalid = rule(); mutation(invalid); assert.throws(() => validateRule(invalid, 'mt4'));
  }
});
test('validation does not mutate rule order, account precision, or source config', () => {
  const group = rule(), account = { ...rule(), id: 'r2', scope: 'account', selector: '18446744073709551615' };
  const config = { schema_version: 1, platform: 'mt5', enabled: true, poll_interval_ms: 1000, rules: [group, account] };
  const original = JSON.stringify(config), validated = validateConfig(config, 'mt5');
  assert.equal(JSON.stringify(config), original); assert.equal(validated.rules[0].scope, 'group');
  assert.equal(validated.rules[1].selector, '18446744073709551615');
  assert.throws(() => validateConfig({ ...config, rules: [] }, 'mt5'));
  assert.throws(() => validateConfig({ ...config, poll_interval_ms: 99 }, 'mt5'));
});
test('names reject Unicode blank-only input without excluding Chinese or emoji', () => {
  for (const name of [' ', '\u3000', '\ufeff', '\u2007', '\u0085']) assert.throws(() => validateRule({ ...rule(), name }, 'mt5'));
  assert.equal(validateRule({ ...rule(), name: 'VIP 账户 🏦' }, 'mt5').name, 'VIP 账户 🏦');
});
test('schema rejects foreign fields, missing fields, invalid IDs, and non-boolean switches', () => {
  for (const mutation of [r => r.unknown = 1, r => delete r.selector, r => r.id = 'rule/unsafe', r => r.id = '', r => r.enabled = 1, r => r.tiers[0].extra = true]) {
    const invalid = rule(); mutation(invalid); assert.throws(() => validateRule(invalid, 'mt5'));
  }
  const config = { schema_version: 1, platform: 'mt5', enabled: true, poll_interval_ms: 1000, rules: [rule()] };
  assert.throws(() => validateConfig({ ...config, enabled: 'true' }, 'mt5'));
  assert.throws(() => validateConfig({ ...config, rules: [rule(), rule()] }, 'mt5'));
  assert.throws(() => validateConfig({ ...config, unknown: false }, 'mt5'));
  assert.throws(() => validateConfig(null, 'mt5'));
  assert.throws(() => validateRule(null, 'mt5'));
});
