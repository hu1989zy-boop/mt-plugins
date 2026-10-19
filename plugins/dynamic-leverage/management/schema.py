"""Versioned dynamic-leverage module contract; no broker credentials or SDK calls."""
from __future__ import annotations

import json
import re

UNITS = 100_000_000
MAX_VOLUME = (1 << 64) - 1
MAX_BYTES = 1024 * 1024
PLATFORMS = ("mt4", "mt5")


class ValidationError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise ValidationError(message)


def fields(value, names, where):
    require(isinstance(value, dict), f"{where}必须是对象")
    require(set(value) == set(names), f"{where}字段不完整或包含未知字段")


def text(value, minimum, maximum, where):
    require(isinstance(value, str), f"{where}必须是文本")
    require(minimum <= len(value) <= maximum, f"{where}长度应为 {minimum}～{maximum} 个字符")
    require(not any(ord(c) < 32 or 127 <= ord(c) <= 159 for c in value), f"{where}不能包含控制字符")
    try:
        value.encode("utf-8")
    except UnicodeError as exc:
        raise ValidationError(f"{where}包含无效字符") from exc
    return value


def integer(value, low, high, where):
    require(type(value) is int and low <= value <= high, f"{where}必须是 {low}～{high} 的整数")
    return value


def lots_units(value):
    text(value, 1, 40, "持仓手数")
    require(re.fullmatch(r"[0-9]+(?:\.[0-9]{1,8})?", value) is not None,
            "手数必须为非负十进制数，最多 8 位小数")
    whole, _, fraction = value.partition(".")
    units = int(whole) * UNITS + int((fraction + "00000000")[:8])
    require(units <= MAX_VOLUME, "持仓手数超出可用范围")
    return units


def format_lots(units):
    whole, fraction = divmod(units, UNITS)
    return str(whole) + (("." + f"{fraction:08d}".rstrip("0")) if fraction else "")


def login_number(value, platform):
    text(value, 1, 255, "交易账号")
    require(re.fullmatch(r"[0-9]+", value) is not None, "交易账号必须是正整数")
    number = int(value)
    limit = (1 << 31) - 1 if platform == "mt4" else (1 << 64) - 1
    require(1 <= number <= limit, f"{platform.upper()} 交易账号超出范围")
    return number


def selector_tokens(selector, scope, platform):
    text(selector, 1, 4096, "匹配条件")
    tokens = [piece.strip(" ") for piece in selector.split(",")]
    require(len(tokens) <= 256, "匹配条件最多包含 256 项")
    require(all(0 < len(item) <= 255 for item in tokens), "匹配条件存在空项或单项超过 255 个字符")
    if scope == "account":
        ids = [login_number(item, platform) for item in tokens]
        require(len(ids) == len(set(ids)), "交易账号不能重复")
        return [str(number) for number in ids]
    for token in tokens:
        pattern = token[1:] if token.startswith("!") else token
        require(bool(pattern) and "!" not in pattern and "?" not in pattern,
                "组别支持 * 通配和前缀 ! 排除；不支持 ?、裸 ! 或重复 !")
    return tokens


def wildcard(pattern, value):
    """Linear-space glob matcher: only * is special, case and path are preserved."""
    p = v = 0
    star = -1
    retry = 0
    while v < len(value):
        if p < len(pattern) and pattern[p] == "*":
            star, retry = p, v
            p += 1
        elif p < len(pattern) and pattern[p] == value[v]:
            p += 1
            v += 1
        elif star >= 0:
            retry += 1
            v, p = retry, star + 1
        else:
            return False
    return all(c == "*" for c in pattern[p:])


def group_matches(selector, group):
    if not group:
        return False
    tokens = selector_tokens(selector, "group", "mt5")
    includes = [token for token in tokens if not token.startswith("!")]
    excludes = [token[1:] for token in tokens if token.startswith("!")]
    return ((not includes or any(wildcard(item, group) for item in includes))
            and not any(wildcard(item, group) for item in excludes))


def default_config(platform):
    require(platform in PLATFORMS, "不支持的平台")
    return {"schema_version": 1, "platform": platform, "enabled": False,
            "poll_interval_ms": 1000, "rules": []}


def validate_config(document, platform):
    """Return a fresh normalized document; never mutate a browser's input object."""
    require(platform in PLATFORMS, "不支持的平台")
    fields(document, ("schema_version", "platform", "enabled", "poll_interval_ms", "rules"), "配置")
    require(type(document["schema_version"]) is int and document["schema_version"] == 1, "不支持的配置版本")
    require(document["platform"] == platform, "配置平台与当前平台不一致")
    require(type(document["enabled"]) is bool, "启用状态必须为布尔值")
    interval = integer(document["poll_interval_ms"], 100, 60000, "轮询间隔")
    require(isinstance(document["rules"], list) and len(document["rules"]) <= 256, "规则必须为数组且不超过 256 条")
    rules, ids = [], set()
    for source in document["rules"]:
        fields(source, ("id", "name", "enabled", "scope", "selector", "tiers"), "规则")
        rid = text(source["id"], 1, 64, "规则标识")
        require(re.fullmatch(r"[A-Za-z0-9_-]+", rid) is not None and rid not in ids, "规则标识格式错误或重复")
        ids.add(rid)
        name = text(source["name"], 1, 80, "规则名称")
        require(bool(name.strip(" \u00a0\u1680\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a\u2028\u2029\u202f\u205f\u3000\ufeff")), "规则名称不能为空白")
        require(type(source["enabled"]) is bool, "规则启用状态必须为布尔值")
        require(source["scope"] in ("group", "account"), "规则类型必须为组别或账户")
        tokens = selector_tokens(source["selector"], source["scope"], platform)
        require(isinstance(source["tiers"], list) and 1 <= len(source["tiers"]) <= 32, "每条规则应包含 1～32 个杠杆档位")
        tiers = []
        previous_volume, previous_leverage = -1, 100001
        for index, tier in enumerate(source["tiers"]):
            fields(tier, ("above_lots", "leverage"), "杠杆档位")
            volume = lots_units(tier["above_lots"])
            leverage = integer(tier["leverage"], 1, 100000, "杠杆")
            require(index != 0 or volume == 0, "首档必须从 0 手开始")
            require(volume > previous_volume, "持仓分档阈值必须严格递增")
            require(leverage < previous_leverage, "持仓增加时杠杆必须逐档降低")
            tiers.append({"above_lots": format_lots(volume), "leverage": leverage})
            previous_volume, previous_leverage = volume, leverage
        rules.append({"id": rid, "name": name, "enabled": source["enabled"],
                      "scope": source["scope"], "selector": ",".join(tokens), "tiers": tiers})
    require(not document["enabled"] or any(rule["enabled"] for rule in rules), "启用动态杠杆前至少需要一条已启用规则")
    return {"schema_version": 1, "platform": platform, "enabled": document["enabled"],
            "poll_interval_ms": interval, "rules": rules}


def decode_json(raw):
    require(len(raw) <= MAX_BYTES, "配置内容超过 1 MiB")
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, f"JSON 字段重复：{key}")
            result[key] = value
        return result
    def constant(_):
        raise ValidationError("JSON 不支持 NaN 或 Infinity")
    try:
        return json.loads(raw.decode("utf-8") if isinstance(raw, bytes) else raw,
                          object_pairs_hook=pairs, parse_constant=constant)
    except (ValueError, UnicodeError, RecursionError) as exc:
        raise ValidationError(f"无效 JSON：{exc}") from exc


def preview(document, platform, login, group, lots):
    config = validate_config(document, platform)
    number = login_number(login, platform)
    text(group, 0, 255, "账户组别")
    volume = lots_units(lots)
    result = {"matched": False, "rule_id": None, "rule_name": None,
              "leverage": None, "volume_lots": format_lots(volume)}
    if not config["enabled"]:
        return result
    for rule in config["rules"]:
        if not rule["enabled"]:
            continue
        matched = (number in [int(x) for x in rule["selector"].split(",")]
                   if rule["scope"] == "account" else bool(group) and group_matches(rule["selector"], group))
        if matched:
            leverage = rule["tiers"][0]["leverage"]
            for tier in rule["tiers"][1:]:
                if volume > lots_units(tier["above_lots"]):
                    leverage = tier["leverage"]
            result.update(matched=True, rule_id=rule["id"], rule_name=rule["name"], leverage=leverage)
            return result
    return result
