import { accountNumber, clone, formatLots, lotsUnits, MAX_VOLUME, newRule, same, SCALE, validateConfig, validateRule } from './rules.mjs';

const $ = id => document.getElementById(id);
const state = { platform: 'mt4', config: null, saved: null, revision: null, csrf: null, loading: false,
  saving: false, conflict: false, loadSequence: 0, previewSequence: 0, auditSequence: 0, controller: null,
  previewController: null, editing: null, editorInitial: null, editorIndex: -1, editorOpener: null, scopeSelectors: {} };
const dirty = () => state.config !== null && !same(state.config, state.saved);
const editorDirty = () => state.editing !== null && !same(state.editing, state.editorInitial);
const locked = () => state.loading || state.saving || state.conflict || !state.config;
const endpoint = (platform = state.platform) => `/api/plugins/dynamic-leverage/${platform}`;
function node(tag, className, content) {
  const el = document.createElement(tag);
  if (className) el.className = className;
  if (content !== undefined) el.textContent = content;
  return el;
}
function icon(name) {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('class', 'icon'); svg.setAttribute('aria-hidden', 'true');
  const use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
  use.setAttribute('href', `#i-${name}`); svg.append(use); return svg;
}
function showMessage(message, type = 'error') {
  $('message-text').textContent = message; $('message').className = `message ${type}`;
  $('message').hidden = false;
}
function clearMessage() { $('message').hidden = true; }
function cancelPreview() {
  state.previewSequence++; state.previewController?.abort(); state.previewController = null;
  $('preview-button').textContent = '测试匹配';
  $('preview-result').hidden = true;
}
function changed() { cancelPreview(); updateControls(); }
async function api(url, options = {}) {
  const response = await fetch(url, { credentials: 'same-origin', cache: 'no-store', ...options,
    headers: { Accept: 'application/json', ...(options.body ? { 'Content-Type': 'application/json', 'X-CSRF-Token': state.csrf } : {}), ...options.headers } });
  let data;
  try { data = await response.json(); } catch { throw new Error(`服务返回了无法读取的响应（HTTP ${response.status}）`); }
  if (!response.ok) {
    const error = new Error(typeof data.error === 'string' ? data.error : `请求失败（HTTP ${response.status}）`);
    error.status = response.status; throw error;
  }
  return data;
}
function updateControls() {
  const isLocked = locked(), hasChanges = dirty();
  $('platform').disabled = state.saving;
  $('new-rule').disabled = isLocked || state.config?.rules.length >= 256;
  $('empty-add').disabled = isLocked; $('example-rule').disabled = isLocked;
  $('global-enabled').disabled = isLocked; $('poll-interval').disabled = isLocked;
  $('save-config').disabled = isLocked || !hasChanges;
  $('revert-config').disabled = isLocked || !hasChanges;
  $('reload-config').disabled = state.loading || state.saving;
  $('preview-button').disabled = isLocked;
  $('save-config').textContent = state.saving ? '正在保存…' : '保存配置';
  $('dirty-dot').className = `state-dot${hasChanges ? ' dirty' : state.config ? ' saved' : ''}`;
  $('save-status').textContent = state.saving ? '正在保存配置' : state.loading ? '正在加载配置' : state.conflict ? '配置已被修改，请重新加载' : !state.config ? '配置未加载' : hasChanges ? '有未保存的修改' : '配置已同步';
  $('save-detail').textContent = `${state.platform.toUpperCase()} · ${hasChanges ? '保存本地规则；逐仓保证金执行待验证' : '本地配置，不代表服务器执行状态'}`;
  $('global-status').textContent = !state.config ? '配置未加载' : state.config.enabled ? '配置总开关已开启' : '配置总开关已关闭';
  for (const control of $('rules-body').querySelectorAll('button,input')) control.disabled = isLocked || control.dataset.unavailable === 'true';
}
function switchControl(rule, index) {
  const label = node('label', 'switch row-switch');
  const input = node('input'); input.type = 'checkbox'; input.checked = rule.enabled;
  input.setAttribute('role', 'switch'); input.setAttribute('aria-label', `${rule.name}：启用规则`);
  input.dataset.ruleId = rule.id; input.dataset.control = 'toggle';
  const track = node('span', 'switch-track');
  const text = node('span', 'switch-label', rule.enabled ? '已启用' : '已停用');
  input.addEventListener('change', () => { if (locked()) return; state.config.rules[index].enabled = input.checked; text.textContent = input.checked ? '已启用' : '已停用'; changed(); });
  label.append(input, track, text); return label;
}
function actionButton(label, image, callback, className = 'icon-button') {
  const button = node('button', className); button.type = 'button'; button.setAttribute('aria-label', label);
  button.title = label; button.append(icon(image)); button.addEventListener('click', callback); return button;
}
function renderRules() {
  const rules = state.config?.rules || []; $('rule-count').textContent = String(rules.length);
  $('loading').hidden = !state.loading;
  $('empty-state').hidden = !state.config || rules.length > 0 || state.loading;
  $('table-wrapper').hidden = !state.config || rules.length === 0 || state.loading;
  $('rules-body').replaceChildren();
  for (const [index, rule] of rules.entries()) {
    const row = node('tr'); row.dataset.ruleId = rule.id;
    const priority = node('td'), controls = node('div', 'priority-control');
    controls.append(node('strong', '', String(index + 1)));
    for (const [offset, direction, title] of [[-1, 'up', '上移'], [1, 'down', '下移']]) {
      const button = actionButton(`${title}规则“${rule.name}”`, direction, () => moveRule(index, offset));
      button.dataset.control = direction; button.dataset.ruleId = rule.id;
      button.dataset.unavailable = String(index + offset < 0 || index + offset >= rules.length);
      controls.append(button);
    }
    priority.append(controls);
    const name = node('td'); name.append(node('span', 'rule-title', rule.name), node('span', `scope-badge ${rule.scope}`, rule.scope === 'group' ? '组别' : '账户'));
    const selector = node('td', 'selector-cell', rule.selector); selector.title = rule.selector;
    const leverageCell = node('td'), summary = node('div', 'tier-summary');
    const displayed = rule.tiers.slice(0, 4);
    displayed.forEach((tier, i) => { if (i) summary.append(node('span', 'arrow', '→')); summary.append(document.createTextNode(String(tier.leverage))); });
    if (rule.tiers.length > 4) summary.append(node('span', 'arrow', '…'));
    summary.append(node('small', '', `${rule.tiers.length} 个档位 · 杠杆 1 : N`));
    summary.title = rule.tiers.map((tier, i) => `${i === 0 ? '0 起' : `>${tier.above_lots}`} 手：1:${tier.leverage}`).join('；'); leverageCell.append(summary);
    const status = node('td'); status.append(switchControl(rule, index));
    const actions = node('td'), actionGroup = node('div', 'row-actions');
    const edit = node('button', 'text-button', '编辑'); edit.type = 'button'; edit.dataset.control = 'edit'; edit.dataset.ruleId = rule.id;
    edit.setAttribute('aria-label', `编辑规则“${rule.name}”`); edit.addEventListener('click', () => openEditor(index));
    const remove = actionButton(`删除规则“${rule.name}”`, 'trash', () => deleteRule(index), 'icon-button danger');
    actionGroup.append(edit, remove); actions.append(actionGroup);
    row.append(priority, name, selector, leverageCell, status, actions); $('rules-body').append(row);
  }
  updateControls();
}
function renderConfig() {
  $('global-enabled').checked = state.config?.enabled || false;
  $('poll-interval').value = state.config?.poll_interval_ms ?? 1000;
  renderRules();
}
let resolveConfirmation = null;
function confirmAction(message, { title = '放弃未保存的修改？', yes = '放弃修改', no = '继续编辑', danger = false } = {}) {
  if (resolveConfirmation) return Promise.resolve(false);
  $('confirm-title').textContent = title; $('confirm-message').textContent = message;
  $('confirm-yes').textContent = yes; $('confirm-no').textContent = no;
  $('confirm-yes').className = `button ${danger ? 'danger' : 'primary'}`;
  $('confirm-dialog').showModal(); $('confirm-no').focus();
  return new Promise(resolve => { resolveConfirmation = resolve; });
}
function settleConfirmation(accepted) {
  const resolve = resolveConfirmation; resolveConfirmation = null;
  $('confirm-dialog').close(); resolve?.(accepted);
}
$('confirm-yes').addEventListener('click', () => settleConfirmation(true));
$('confirm-no').addEventListener('click', () => settleConfirmation(false));
$('confirm-dialog').addEventListener('cancel', event => { event.preventDefault(); settleConfirmation(false); });

async function loadConfig(platform) {
  state.controller?.abort(); cancelPreview();
  const controller = new AbortController(), sequence = ++state.loadSequence;
  state.controller = controller; state.loading = true; state.platform = platform;
  state.config = null; state.saved = null; state.revision = null; state.conflict = false;
  $('platform').value = platform; $('config-path').textContent = '正在读取配置位置…';
  $('audit-list').replaceChildren(node('li', '', '正在读取保存记录…'));
  clearMessage(); renderConfig();
  try {
    const session = await api('/api/session', { signal: controller.signal });
    if (sequence !== state.loadSequence) return;
    if (typeof session.csrf_token !== 'string' || !session.csrf_token) throw new Error('管理会话不可用，请重新加载');
    state.csrf = session.csrf_token;
    const data = await api(`${endpoint(platform)}/config`, { signal: controller.signal });
    if (sequence !== state.loadSequence) return;
    validateConfig(data.config, platform);
    if (typeof data.revision !== 'string' || !data.revision) throw new Error('缺少配置版本，无法安全编辑');
    state.config = clone(data.config); state.saved = clone(data.config); state.revision = data.revision;
    $('config-path').textContent = data.path || '管理服务未返回配置路径';
    if (!data.exists) showMessage('此平台尚无配置文件。添加规则并保存后创建；默认总开关保持关闭。', 'info');
    loadAudit(platform, sequence);
  } catch (error) {
    if (error.name === 'AbortError' || sequence !== state.loadSequence) return;
    showMessage(`读取配置失败：${error.message}`);
    $('config-path').textContent = '配置读取失败'; $('audit-list').replaceChildren(node('li', '', '保存记录暂不可用'));
  } finally {
    if (sequence === state.loadSequence) { state.loading = false; renderConfig(); }
  }
}
async function loadAudit(platform, sequence = state.loadSequence) {
  const auditSequence = ++state.auditSequence;
  try {
    const data = await api(`${endpoint(platform)}/audit`);
    if (sequence !== state.loadSequence || platform !== state.platform || auditSequence !== state.auditSequence) return;
    const entries = Array.isArray(data.entries) ? data.entries : [];
    $('audit-list').replaceChildren(...(entries.length ? entries.slice(0, 8).map(entry => node('li', '', `${entry.time} · ${String(entry.action)} · ${entry.rule_count} 条规则 · ${String(entry.revision).slice(0, 10)}`)) : [node('li', '', '暂无保存记录')]));
  } catch { if (sequence === state.loadSequence && platform === state.platform && auditSequence === state.auditSequence) $('audit-list').replaceChildren(node('li', '', '保存记录暂不可用')); }
}
async function saveConfig() {
  if (locked() || !dirty()) return;
  let document;
  try { document = validateConfig(state.config, state.platform); }
  catch (error) { showMessage(error.message); return; }
  state.saving = true; cancelPreview(); clearMessage(); updateControls();
  const platform = state.platform;
  try {
    const data = await api(`${endpoint(platform)}/config`, { method: 'PUT', headers: { 'If-Match': state.revision }, body: JSON.stringify(document) });
    if (typeof data.revision !== 'string' || !data.revision) throw new Error('保存响应缺少配置版本，请重新加载确认');
    validateConfig(data.config, platform);
    state.config = clone(data.config); state.saved = clone(data.config); state.revision = data.revision;
    $('config-path').textContent = data.path || $('config-path').textContent;
    showMessage(`${platform.toUpperCase()} 配置已保存。逐仓保证金执行尚在验证，当前保存仅更新本地规则。`, 'success');
    loadAudit(platform);
  } catch (error) {
    if (error.status === 409) {
      state.conflict = true;
      showMessage('其他窗口或程序已修改配置。为避免覆盖，当前草稿未保存；请点击“重新加载”获取最新配置后再编辑。');
    } else showMessage(`未能确认保存结果：${error.message}。当前草稿仍保留；可重新加载核对配置。`);
  } finally { state.saving = false; renderConfig(); }
}
function moveRule(index, offset) {
  if (locked()) return;
  const target = index + offset;
  if (target < 0 || target >= state.config.rules.length) return;
  const [rule] = state.config.rules.splice(index, 1); state.config.rules.splice(target, 0, rule);
  changed(); renderRules();
  const direction = offset < 0 ? 'up' : 'down';
  const button = [...$('rules-body').querySelectorAll('button')].find(el => el.dataset.ruleId === rule.id && el.dataset.control === direction);
  if (button && !button.disabled) button.focus(); else [...$('rules-body').querySelectorAll('button')].find(el => el.dataset.ruleId === rule.id && el.dataset.control === 'edit')?.focus();
  showMessage(`“${rule.name}”已调整为第 ${target + 1} 优先级，请保存配置。`, 'info');
}
async function deleteRule(index) {
  if (locked()) return;
  const rule = state.config.rules[index];
  if (!await confirmAction(`删除“${rule.name}”？保存配置后将移除此规则。`, { title: '删除规则', yes: '删除规则', no: '保留规则', danger: true })) return;
  state.config.rules.splice(index, 1); changed(); renderRules();
  const nextRow = $('rules-body').children[Math.min(index, state.config.rules.length - 1)];
  if (nextRow) nextRow.querySelector('[data-control="edit"]')?.focus(); else $('new-rule').focus();
}
function updateScope() {
  const group = state.editing.scope === 'group';
  $('selector-label').textContent = group ? '账户组别' : '交易账户';
  $('rule-selector').placeholder = group ? '例如 !demo*,real*,vip*' : '例如 10001,10002';
  $('selector-help').textContent = group ? '多个条件用英文逗号分隔；支持 * 通配、! 排除，区分大小写。排除条件优先；仅填写排除条件时默认包含其余组别。' : `多个交易账号用英文逗号分隔。账号按完整整数文本保存，支持 ${state.platform.toUpperCase()} 账号范围。`;
  $('group-examples').hidden = !group;
}
function renderTiers() {
  const tiers = state.editing.tiers;
  $('tiers-body').replaceChildren();
  for (const [index, tier] of tiers.entries()) {
    const row = node('tr'), rangeCell = node('td'), range = node('div', 'tier-range');
    const lower = node('span', 'tier-lower', tier.above_lots || '—'); lower.dataset.lowerIndex = String(index); lower.title = tier.above_lots;
    range.append(lower, node('span', '', index === 0 ? '≤' : '<'), node('span', 'tier-volume', '持仓'));
    if (index < tiers.length - 1) {
      range.append(node('span', '', '≤'));
      const upper = node('input'); upper.type = 'text'; upper.inputMode = 'decimal'; upper.maxLength = 40;
      upper.value = tiers[index + 1].above_lots; upper.setAttribute('aria-label', `第 ${index + 1} 档持仓上限（手）`);
      upper.addEventListener('input', () => {
        state.editing.tiers[index + 1].above_lots = upper.value;
        const nextLower = $('tiers-body').querySelector(`[data-lower-index="${index + 1}"]`);
        nextLower.textContent = upper.value || '—'; nextLower.title = upper.value; $('rule-error').hidden = true;
      }); range.append(upper);
    } else range.append(node('span', 'unlimited', '无上限'));
    rangeCell.append(range);
    const leverageCell = node('td'), leverage = node('div', 'tier-leverage'); leverage.append(node('span', '', '1 :'));
    const input = node('input'); input.type = 'text'; input.inputMode = 'numeric'; input.maxLength = 6; input.value = String(tier.leverage);
    input.setAttribute('aria-label', `第 ${index + 1} 档杠杆`);
    input.addEventListener('input', () => { state.editing.tiers[index].leverage = input.value; $('rule-error').hidden = true; });
    leverage.append(input); leverageCell.append(leverage);
    const removeCell = node('td'), remove = actionButton(`删除第 ${index + 1} 档`, 'trash', () => {
      if (tiers.length <= 1) return;
      state.editing.tiers.splice(index, 1); state.editing.tiers[0].above_lots = '0'; renderTiers(); $('rule-error').hidden = true;
    }); remove.disabled = tiers.length === 1; removeCell.append(remove); row.append(rangeCell, leverageCell, removeCell); $('tiers-body').append(row);
  }
  $('add-tier').disabled = tiers.length >= 32;
}
function openEditor(index = -1) {
  if (locked() || (index < 0 && state.config.rules.length >= 256)) return;
  state.editorOpener = document.activeElement;
  state.editorIndex = index; state.editing = index < 0 ? newRule(`规则 ${state.config.rules.length + 1}`) : clone(state.config.rules[index]);
  state.editorInitial = clone(state.editing);
  state.scopeSelectors = { group: '', account: '', [state.editing.scope]: state.editing.selector };
  $('dialog-title').textContent = index < 0 ? '新增规则' : '编辑规则';
  $('rule-name').value = state.editing.name; $('rule-selector').value = state.editing.selector; $('rule-enabled').checked = state.editing.enabled;
  for (const input of document.querySelectorAll('input[name=scope]')) input.checked = input.value === state.editing.scope;
  $('rule-error').hidden = true; updateScope(); renderTiers(); $('rule-dialog').showModal(); $('rule-dialog').scrollTop = 0;
  $('rule-name').focus(); $('rule-name').select();
}
function finishEditor() {
  const opener = state.editorOpener;
  state.editing = null; state.editorInitial = null; state.editorIndex = -1;
  $('rule-dialog').close(); if (opener?.isConnected && !opener.disabled) opener.focus(); else $('new-rule').focus();
}
async function cancelEditor() {
  if (editorDirty() && !await confirmAction('此规则的编辑内容尚未加入页面配置。')) return;
  finishEditor();
}
$('rule-form').addEventListener('submit', event => {
  event.preventDefault();
  try {
    const rule = validateRule(state.editing, state.platform);
    if (state.editorIndex < 0) state.config.rules.push(rule); else state.config.rules[state.editorIndex] = rule;
    finishEditor(); changed(); renderRules(); showMessage('规则已更新到页面草稿，请点击“保存配置”写入配置文件。', 'info');
    [...$('rules-body').querySelectorAll('[data-control="edit"]')].find(button => button.dataset.ruleId === rule.id)?.focus();
  } catch (error) { $('rule-error').textContent = error.message; $('rule-error').hidden = false; $('rule-error').scrollIntoView({ block: 'nearest' }); }
});
$('rule-name').addEventListener('input', event => { state.editing.name = event.target.value; $('rule-error').hidden = true; });
$('rule-selector').addEventListener('input', event => { state.editing.selector = event.target.value; $('rule-error').hidden = true; });
$('rule-enabled').addEventListener('change', event => { state.editing.enabled = event.target.checked; });
for (const input of document.querySelectorAll('input[name=scope]')) input.addEventListener('change', () => {
  // Preserve each selector while switching type, but only one is saved in the rule.
  const previousScope = state.editing.scope;
  state.scopeSelectors[previousScope] = state.editing.selector;
  state.editing.scope = input.value;
  state.editing.selector = state.scopeSelectors[input.value] || '';
  if (previousScope !== input.value) { $('rule-selector').value = state.editing.selector; updateScope(); $('rule-error').hidden = true; }
});
$('add-tier').addEventListener('click', () => {
  const tiers = state.editing.tiers; if (tiers.length >= 32) return;
  try {
    const last = tiers.at(-1); const units = lotsUnits(last.above_lots), next = units + SCALE;
    if (next > MAX_VOLUME) throw new Error('手数已达可用上限，无法继续添加档位');
    const leverage = Number(last.leverage);
    if (!Number.isInteger(leverage) || leverage <= 1 || leverage > 100000) throw new Error('请先设置有效的末档杠杆；杠杆为 1 时无法继续降低');
    tiers.push({ above_lots: formatLots(next), leverage: Math.max(1, Math.floor(leverage / 2)) });
    renderTiers(); $('rule-error').hidden = true;
    $('tiers-body').lastElementChild?.querySelector('input')?.focus();
  } catch (error) { $('rule-error').textContent = error.message; $('rule-error').hidden = false; $('rule-error').scrollIntoView({ block: 'nearest' }); }
});
$('close-rule').addEventListener('click', cancelEditor); $('cancel-rule').addEventListener('click', cancelEditor);
$('rule-dialog').addEventListener('cancel', event => { event.preventDefault(); cancelEditor(); });
for (const id of ['new-rule', 'empty-add']) $(id).addEventListener('click', () => openEditor());
$('example-rule').addEventListener('click', () => {
  if (locked() || state.config.rules.length) return;
  const rule = newRule('标准持仓杠杆'); rule.selector = '!demo*,*'; state.config.rules.push(rule);
  changed(); renderRules(); showMessage('已填入示例：排除 demo* 组别，按 0～2 / 超过 2～5 / 超过 5 手设置 1000 / 500 / 200。检查后请保存配置。', 'info');
});
$('global-enabled').addEventListener('change', event => { if (!locked()) { state.config.enabled = event.target.checked; changed(); } });
$('poll-interval').addEventListener('input', event => { if (!locked()) { state.config.poll_interval_ms = event.target.value === '' ? null : Number(event.target.value); changed(); } });
$('platform').addEventListener('change', async event => {
  const next = event.target.value; $('platform').value = state.platform;
  if (next === state.platform) return;
  if (dirty() && !await confirmAction(`切换到 ${next.toUpperCase()} 将放弃当前 ${state.platform.toUpperCase()} 的未保存修改。`, { yes: '放弃并切换' })) return;
  loadConfig(next);
});
$('reload-config').addEventListener('click', async () => {
  if (dirty() && !await confirmAction('重新加载会放弃当前页面草稿，读取最新的配置文件。', { yes: '放弃并加载' })) return;
  loadConfig(state.platform);
});
$('revert-config').addEventListener('click', async () => {
  if (locked() || !dirty() || !await confirmAction('将恢复到本页面最近读取或保存的配置。', { title: '撤销所有未保存修改？', yes: '撤销修改' })) return;
  state.config = clone(state.saved); clearMessage(); changed(); renderConfig();
});
$('save-config').addEventListener('click', saveConfig); $('message-dismiss').addEventListener('click', clearMessage);
$('preview-form').addEventListener('submit', async event => {
  event.preventDefault(); if (locked()) return;
  cancelPreview(); const sequence = state.previewSequence, platform = state.platform;
  const controller = new AbortController(); state.previewController = controller;
  $('preview-button').textContent = '正在计算…'; $('preview-button').disabled = true;
  try {
    const config = validateConfig(state.config, platform);
    const login = accountNumber($('preview-login').value, platform);
    const group = $('preview-group').value, lots = $('preview-lots').value;
    if ([...group].length > 255 || /[\u0000-\u001f\u007f-\u009f]/u.test(group)) throw new Error('账户组别最多 255 个字符，不能包含控制字符');
    lotsUnits(lots);
    const result = await api(`${endpoint(platform)}/preview`, { method: 'POST', signal: controller.signal, body: JSON.stringify({ config, login, group, lots }) });
    if (sequence !== state.previewSequence || platform !== state.platform) return;
    const output = $('preview-result'); output.className = 'preview-result'; output.replaceChildren();
    if (result.matched) {
      const index = config.rules.findIndex(rule => rule.id === result.rule_id);
      output.append(document.createTextNode(`命中第 ${index + 1} 条规则“${result.rule_name}” · 开仓后 ${result.volume_lots} 手 · 新仓试算`), node('strong', '', `1 : ${result.leverage}`));
    } else output.textContent = config.enabled ? '没有命中已启用的规则；此配置不会为该账户指定杠杆。' : '当前配置总开关已关闭，不应用任何动态杠杆规则。开启总开关后可预览匹配结果。';
    output.hidden = false;
  } catch (error) {
    if (error.name === 'AbortError' || sequence !== state.previewSequence) return;
    $('preview-result').className = 'preview-result error'; $('preview-result').textContent = error.message; $('preview-result').hidden = false;
  } finally { if (sequence === state.previewSequence) { state.previewController = null; $('preview-button').textContent = '测试匹配'; updateControls(); } }
});
for (const id of ['preview-login', 'preview-group', 'preview-lots']) $(id).addEventListener('input', () => { cancelPreview(); updateControls(); });
window.addEventListener('beforeunload', event => { if (dirty() || editorDirty() || state.saving) { event.preventDefault(); event.returnValue = ''; } });
for (const link of document.querySelectorAll('a[href="./"]')) link.addEventListener('click', async event => {
  event.preventDefault(); if (state.saving) return;
  if (dirty() && !await confirmAction('返回动态杠杆页面将重新加载配置，当前未保存的修改会丢失。')) return;
  loadConfig(state.platform);
});
loadConfig('mt4');
