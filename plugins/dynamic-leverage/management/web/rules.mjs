// Decimal quantities and account identifiers remain strings at every I/O boundary.
export const SCALE = 100000000n;
export const MAX_VOLUME = 18446744073709551615n;
const textLength = value => [...value].length;
function assert(condition, message) { if (!condition) throw new Error(message); }
function fields(value, names, label) {
  assert(value !== null && typeof value === 'object' && !Array.isArray(value), `${label}必须是对象`);
  const keys = Object.keys(value);
  assert(keys.length === names.length && keys.every(key => names.includes(key)), `${label}字段不完整或包含未知字段`);
}
function safeText(value, min, max, label) {
  assert(typeof value === 'string' && textLength(value) >= min && textLength(value) <= max, `${label}长度应为 ${min}～${max} 个字符`);
  assert(!/[\u0000-\u001f\u007f-\u009f]/u.test(value), `${label}不能包含控制字符`);
  assert(!/[\ud800-\udfff]/u.test(value), `${label}包含无效字符`);
  return value;
}
export function lotsUnits(value) {
  safeText(value, 1, 40, '持仓手数');
  assert(/^[0-9]+(?:\.[0-9]{1,8})?$/.test(value), '手数必须为非负十进制数，最多 8 位小数');
  const [whole, part = ''] = value.split('.');
  const units = BigInt(whole) * SCALE + BigInt(part.padEnd(8, '0'));
  assert(units <= MAX_VOLUME, '持仓手数超出可用范围');
  return units;
}
export function formatLots(units) {
  const fraction = String(units % SCALE).padStart(8, '0').replace(/0+$/, '');
  return String(units / SCALE) + (fraction ? `.${fraction}` : '');
}
export function accountNumber(value, platform) {
  safeText(value, 1, 255, '交易账号');
  assert(/^[0-9]+$/.test(value), '交易账号必须是正整数');
  const account = BigInt(value);
  assert(account > 0n && account <= (platform === 'mt4' ? 2147483647n : 18446744073709551615n), `${platform.toUpperCase()} 交易账号超出范围`);
  return String(account);
}
export function selectorTokens(selector, scope, platform) {
  safeText(selector, 1, 4096, '匹配条件');
  const tokens = selector.split(',').map(item => item.replace(/^ +| +$/g, ''));
  assert(tokens.length <= 256, '匹配条件最多包含 256 项');
  assert(tokens.every(item => textLength(item) > 0 && textLength(item) <= 255), '匹配条件存在空项或单项超过 255 个字符');
  if (scope === 'account') {
    const accounts = tokens.map(item => accountNumber(item, platform));
    assert(new Set(accounts).size === accounts.length, '交易账号不能重复');
    return accounts;
  }
  for (const token of tokens) {
    const pattern = token.startsWith('!') ? token.slice(1) : token;
    assert(pattern.length > 0 && !/[!?]/.test(pattern), '组别支持 * 通配和前缀 ! 排除；不支持 ?、裸 ! 或重复 !');
  }
  return tokens;
}
export function validateRule(source, platform) {
  fields(source, ['id', 'name', 'enabled', 'scope', 'selector', 'tiers'], '规则');
  const rule = structuredClone(source);
  safeText(rule.id, 1, 64, '规则标识');
  assert(/^[A-Za-z0-9_-]+$/.test(rule.id), '规则标识格式错误');
  safeText(rule.name, 1, 80, '规则名称');
  assert(rule.name.trim().length > 0, '规则名称不能为空白');
  assert(typeof rule.enabled === 'boolean', '规则启用状态必须为布尔值');
  assert(['group', 'account'].includes(rule.scope), '请选择组别或账户');
  rule.selector = selectorTokens(rule.selector, rule.scope, platform).join(',');
  assert(Array.isArray(rule.tiers) && rule.tiers.length >= 1 && rule.tiers.length <= 32, '每条规则应包含 1～32 个杠杆档位');
  let previous = -1n, previousLeverage = 100001;
  rule.tiers = rule.tiers.map((tier, index) => {
    fields(tier, ['above_lots', 'leverage'], '杠杆档位');
    const volume = lotsUnits(tier.above_lots);
    const leverage = typeof tier.leverage === 'string' && /^[0-9]+$/.test(tier.leverage) ? Number(tier.leverage) : tier.leverage;
    assert(Number.isInteger(leverage) && leverage >= 1 && leverage <= 100000, `第 ${index + 1} 档杠杆应为 1～100000 的整数`);
    assert(index !== 0 || volume === 0n, '首档必须从 0 手开始');
    assert(volume > previous, '持仓分档阈值必须严格递增');
    assert(leverage < previousLeverage, '持仓增加时杠杆必须逐档降低');
    previous = volume; previousLeverage = leverage;
    return { above_lots: formatLots(volume), leverage };
  });
  return rule;
}
export function validateConfig(source, platform) {
  fields(source, ['schema_version', 'platform', 'enabled', 'poll_interval_ms', 'rules'], '配置');
  const config = structuredClone(source);
  assert(['mt4', 'mt5'].includes(platform) && config.schema_version === 1 && config.platform === platform, '配置版本或平台不匹配，请重新加载');
  assert(typeof config.enabled === 'boolean', '启用状态必须为布尔值');
  assert(Number.isInteger(config.poll_interval_ms) && config.poll_interval_ms >= 100 && config.poll_interval_ms <= 60000, '轮询间隔必须为 100～60000 毫秒的整数');
  assert(Array.isArray(config.rules) && config.rules.length <= 256, '最多可设置 256 条规则');
  config.rules = config.rules.map(rule => validateRule(rule, platform));
  assert(new Set(config.rules.map(rule => rule.id)).size === config.rules.length, '规则标识不能重复');
  assert(!config.enabled || config.rules.some(rule => rule.enabled), '启用动态杠杆前至少需要一条已启用规则');
  return config;
}
export function newRule(name = '新规则') {
  return { id: `rule-${crypto.randomUUID()}`, name, enabled: true, scope: 'group', selector: '', tiers: [
    { above_lots: '0', leverage: 1000 }, { above_lots: '2', leverage: 500 }, { above_lots: '5', leverage: 200 },
  ] };
}
export const clone = value => structuredClone(value);
export const same = (left, right) => JSON.stringify(left) === JSON.stringify(right);
