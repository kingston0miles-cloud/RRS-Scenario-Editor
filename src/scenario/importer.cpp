#include    "scenario/importer.h"

#include    "scenario/consist.h"

#include    <QDir>
#include    <QFile>
#include    <QFileInfo>
#include    <QHash>
#include    <QRegularExpression>
#include    <QSet>
#include    <QXmlStreamReader>

#include    <algorithm>
#include    <optional>

namespace rse
{

namespace
{

//------------------------------------------------------------------------------
// Лексер Lua: достаточно, чтобы делить файл на операторы и разбирать вызовы
//------------------------------------------------------------------------------
enum class TK { Ident, Number, String, Punct };

struct Tok
{
    TK kind = TK::Punct;
    QString text;      // для строк - раскодированное значение
    int start = 0;
    int end = 0;
    int line = 0;
};

struct Lexed
{
    QString src;
    QVector<Tok> toks;
    bool ok = true;
};

Lexed lex(const QString &src)
{
    Lexed L;

    L.src = src;

    int i = 0;
    int line = 0;
    const int n = src.size();

    auto push = [&](TK k, const QString &text, int s, int e)
    {
        Tok t;
        t.kind = k;
        t.text = text;
        t.start = s;
        t.end = e;
        t.line = line;
        L.toks.push_back(t);
    };

    // Длинная скобка [[ ... ]] или [=[ ... ]=]: возвращает индекс после закрывающей или -1
    auto long_bracket = [&](int pos, QString *content) -> int
    {
        int j = pos + 1;
        int level = 0;

        while (j < n && src[j] == QLatin1Char('='))
        {
            ++level;
            ++j;
        }

        if (j >= n || src[j] != QLatin1Char('['))
        {
            return -1;
        }

        const QString close = QStringLiteral("]") + QString(level, QLatin1Char('=')) + QStringLiteral("]");
        const int e = src.indexOf(close, j + 1);

        if (e < 0)
        {
            return -2;
        }

        if (content != nullptr)
        {
            *content = src.mid(j + 1, e - j - 1);
        }

        return e + close.size();
    };

    while (i < n)
    {
        const QChar c = src[i];

        if (c == QLatin1Char('\n'))
        {
            ++line;
            ++i;
            continue;
        }

        if (c.isSpace())
        {
            ++i;
            continue;
        }

        // Комментарии
        if (c == QLatin1Char('-') && i + 1 < n && src[i + 1] == QLatin1Char('-'))
        {
            if (i + 2 < n && src[i + 2] == QLatin1Char('['))
            {
                const int e = long_bracket(i + 2, nullptr);

                if (e >= 0)
                {
                    line += src.mid(i, e - i).count(QLatin1Char('\n'));
                    i = e;
                    continue;
                }

                if (e == -2)
                {
                    L.ok = false;
                    return L;
                }
            }

            while (i < n && src[i] != QLatin1Char('\n'))
            {
                ++i;
            }

            continue;
        }

        if (c == QLatin1Char('"') || c == QLatin1Char('\''))
        {
            const int s = i;
            QString val;
            ++i;

            while (i < n && src[i] != c)
            {
                if (src[i] == QLatin1Char('\n'))
                {
                    L.ok = false;
                    return L;
                }

                if (src[i] == QLatin1Char('\\') && i + 1 < n)
                {
                    ++i;

                    switch (src[i].unicode())
                    {
                    case 'n': val += QLatin1Char('\n'); break;
                    case 't': val += QLatin1Char('\t'); break;
                    default: val += src[i]; break;
                    }
                }
                else
                {
                    val += src[i];
                }

                ++i;
            }

            if (i >= n)
            {
                L.ok = false;
                return L;
            }

            ++i;
            push(TK::String, val, s, i);
            continue;
        }

        if (c == QLatin1Char('['))
        {
            QString content;
            const int e = long_bracket(i, &content);

            if (e >= 0)
            {
                const int s = i;
                push(TK::String, content, s, e);
                line += src.mid(s, e - s).count(QLatin1Char('\n'));
                i = e;
                continue;
            }

            if (e == -2)
            {
                L.ok = false;
                return L;
            }
        }

        if (c.isDigit() || (c == QLatin1Char('.') && i + 1 < n && src[i + 1].isDigit()))
        {
            const int s = i;

            while (i < n && (src[i].isLetterOrNumber() || src[i] == QLatin1Char('.')))
            {
                ++i;
            }

            push(TK::Number, src.mid(s, i - s), s, i);
            continue;
        }

        if (c.isLetter() || c == QLatin1Char('_'))
        {
            const int s = i;

            while (i < n && (src[i].isLetterOrNumber() || src[i] == QLatin1Char('_')))
            {
                ++i;
            }

            push(TK::Ident, src.mid(s, i - s), s, i);
            continue;
        }

        // Многосимвольные знаки
        static const QStringList multi = { QStringLiteral("..."), QStringLiteral(".."), QStringLiteral("=="),
                                           QStringLiteral("~="), QStringLiteral("<="), QStringLiteral(">="),
                                           QStringLiteral("::") };
        bool matched = false;

        for (const QString &m : multi)
        {
            if (src.mid(i, m.size()) == m)
            {
                push(TK::Punct, m, i, i + m.size());
                i += m.size();
                matched = true;
                break;
            }
        }

        if (!matched)
        {
            push(TK::Punct, QString(c), i, i + 1);
            ++i;
        }
    }

    return L;
}

//------------------------------------------------------------------------------
// Деление на операторы верхнего уровня (диапазоны индексов лексем)
//------------------------------------------------------------------------------
struct Range
{
    int a = 0;   // первая лексема
    int b = 0;   // после последней
};

bool isIdent(const Tok &t, const char *s)
{
    return t.kind == TK::Ident && t.text == QLatin1String(s);
}

bool isPunct(const Tok &t, const char *s)
{
    return t.kind == TK::Punct && t.text == QLatin1String(s);
}

QVector<Range> splitStatements(const QVector<Tok> &toks, int from, int to)
{
    QVector<Range> out;

    int i = from;

    while (i < to)
    {
        // пропуск разделителей
        if (isPunct(toks[i], ";"))
        {
            ++i;
            continue;
        }

        const int start = i;
        int block = 0;
        int paren = 0;
        int do_skip = 0;

        for (; i < to; ++i)
        {
            const Tok &t = toks[i];

            if (t.kind == TK::Ident)
            {
                const QString &w = t.text;

                if (w == QLatin1String("function") || w == QLatin1String("if") || w == QLatin1String("repeat"))
                {
                    ++block;
                }
                else if (w == QLatin1String("for") || w == QLatin1String("while"))
                {
                    ++block;
                    ++do_skip;
                }
                else if (w == QLatin1String("do"))
                {
                    if (do_skip > 0)
                    {
                        --do_skip;
                    }
                    else
                    {
                        ++block;
                    }
                }
                else if (w == QLatin1String("end") || w == QLatin1String("until"))
                {
                    --block;
                }
            }
            else if (t.kind == TK::Punct)
            {
                if (t.text == QLatin1String("(") || t.text == QLatin1String("{") || t.text == QLatin1String("["))
                {
                    ++paren;
                }
                else if (t.text == QLatin1String(")") || t.text == QLatin1String("}") || t.text == QLatin1String("]"))
                {
                    --paren;
                }
            }

            if (block <= 0 && paren <= 0)
            {
                // оператор закончился, если следующая лексема на другой строке и
                // текущая не оставляет выражение незавершённым
                const bool next_new_line = (i + 1 >= to) || (toks[i + 1].line > t.line);

                bool dangling = false;

                if (t.kind == TK::Punct)
                {
                    static const QStringList ops = { QStringLiteral(","), QStringLiteral("="), QStringLiteral(".."),
                                                     QStringLiteral("+"), QStringLiteral("-"), QStringLiteral("*"),
                                                     QStringLiteral("/"), QStringLiteral("."), QStringLiteral(":") };
                    dangling = ops.contains(t.text);
                }
                else if (t.kind == TK::Ident)
                {
                    dangling = (t.text == QLatin1String("and") || t.text == QLatin1String("or")
                                || t.text == QLatin1String("not") || t.text == QLatin1String("local"));
                }

                if (next_new_line && !dangling)
                {
                    ++i;
                    break;
                }
            }
        }

        out.push_back({ start, i });
    }

    return out;
}

//------------------------------------------------------------------------------
// Значения и разбор вызовов
//------------------------------------------------------------------------------
struct Value
{
    enum Kind { Str, Num, Bool } kind = Str;
    QString s;
    double n = 0.0;
    bool b = false;
};

struct TrainRec
{
    QString var;
    QString name;
    QString config;
    QString traj;
    double coord = 0.0;
    int dir = 1;
    bool autoFlag = false;
    bool hasName = false;
    bool hasConfig = false;
    bool hasTraj = false;
    bool ok = true;

    QStringList raw;           // исходные операторы поезда
};

struct FuncRec
{
    QString name;
    QVector<EventAction> actions;
    int delaySec = -1;            // >= 0: тело - обёртка setPostEventTimeTrigger
    bool convertible = false;
    int opaqueIndex = -1;         // индекс в списке «до» для возможного удаления
    bool consumed = false;
    QString raw;
};

struct Parser
{
    const Lexed &L;
    QHash<QString, Value> consts;
    QHash<QString, TrainRec> trains;

    explicit Parser(const Lexed &l) : L(l) {}

    const QVector<Tok> &T() const
    {
        return L.toks;
    }

    // Разбор «имя(арг, арг)» на диапазоне [a, b). args - диапазоны аргументов
    bool parseCall(int a, int b, QString *name, QVector<Range> *args) const
    {
        if (a >= b || T()[a].kind != TK::Ident)
        {
            return false;
        }

        QString nm = T()[a].text;
        int i = a + 1;

        while (i + 1 < b && isPunct(T()[i], ".") && T()[i + 1].kind == TK::Ident)
        {
            nm += QLatin1Char('.') + T()[i + 1].text;
            i += 2;
        }

        if (i >= b || !isPunct(T()[i], "(") || !isPunct(T()[b - 1], ")"))
        {
            return false;
        }

        // закрывающая скобка должна быть последней
        int depth = 0;

        for (int j = i; j < b; ++j)
        {
            if (isPunct(T()[j], "(") || isPunct(T()[j], "{") || isPunct(T()[j], "["))
            {
                ++depth;
            }
            else if (isPunct(T()[j], ")") || isPunct(T()[j], "}") || isPunct(T()[j], "]"))
            {
                --depth;

                if (depth == 0 && j != b - 1)
                {
                    return false;
                }
            }
        }

        args->clear();

        int start = i + 1;
        depth = 0;

        for (int j = i + 1; j < b - 1; ++j)
        {
            const Tok &t = T()[j];

            if (isPunct(t, "(") || isPunct(t, "{") || isPunct(t, "["))
            {
                ++depth;
            }
            else if (isPunct(t, ")") || isPunct(t, "}") || isPunct(t, "]"))
            {
                --depth;
            }
            else if (depth == 0 && isPunct(t, ","))
            {
                args->push_back({ start, j });
                start = j + 1;
            }
        }

        if (start < b - 1)
        {
            args->push_back({ start, b - 1 });
        }
        else if (start == b - 1 && !args->isEmpty())
        {
            return false;   // «f(a,)»
        }

        *name = nm;

        return true;
    }

    std::optional<Value> eval(Range r) const
    {
        const QVector<Tok> &t = T();

        if (r.a >= r.b)
        {
            return std::nullopt;
        }

        Value v;

        if (r.b - r.a == 1)
        {
            const Tok &x = t[r.a];

            if (x.kind == TK::String)
            {
                v.kind = Value::Str;
                v.s = x.text;
                return v;
            }

            if (x.kind == TK::Number)
            {
                bool ok = false;
                v.kind = Value::Num;
                v.n = x.text.toDouble(&ok);

                if (!ok)
                {
                    return std::nullopt;
                }

                return v;
            }

            if (x.kind == TK::Ident)
            {
                if (x.text == QLatin1String("true") || x.text == QLatin1String("false"))
                {
                    v.kind = Value::Bool;
                    v.b = (x.text == QLatin1String("true"));
                    return v;
                }

                auto it = consts.constFind(x.text);

                if (it != consts.constEnd())
                {
                    return it.value();
                }
            }

            return std::nullopt;
        }

        // -число
        if (r.b - r.a == 2 && isPunct(t[r.a], "-") && t[r.a + 1].kind == TK::Number)
        {
            bool ok = false;
            v.kind = Value::Num;
            v.n = -t[r.a + 1].text.toDouble(&ok);

            return ok ? std::optional<Value>(v) : std::nullopt;
        }

        // var.поле
        if (r.b - r.a == 3 && t[r.a].kind == TK::Ident && isPunct(t[r.a + 1], ".")
            && t[r.a + 2].kind == TK::Ident)
        {
            auto it = trains.constFind(t[r.a].text);

            if (it == trains.constEnd() || !it->ok)
            {
                return std::nullopt;
            }

            const QString f = t[r.a + 2].text;

            if (f == QLatin1String("name") && it->hasName) { v.s = it->name; return v; }
            if (f == QLatin1String("config") && it->hasConfig) { v.s = it->config; return v; }
            if (f == QLatin1String("traj") && it->hasTraj) { v.s = it->traj; return v; }

            if (f == QLatin1String("coord")) { v.kind = Value::Num; v.n = it->coord; return v; }
            if (f == QLatin1String("dir")) { v.kind = Value::Num; v.n = it->dir; return v; }
        }

        return std::nullopt;
    }

    bool str(Range r, QString *out) const
    {
        const auto v = eval(r);

        if (!v || v->kind != Value::Str)
        {
            return false;
        }

        *out = v->s;

        return true;
    }

    bool intval(Range r, int *out) const
    {
        const auto v = eval(r);

        if (!v || v->kind != Value::Num)
        {
            return false;
        }

        *out = static_cast<int>(v->n);

        return true;
    }

    // Действие из вызова функции-фабрики (actionBuildTrainRoute(...)) или прямого вызова
    // (buildTrainRoute(...)); direct - прямой вызов
    bool actionFromCall(const QString &name, const QVector<Range> &args, bool direct,
                        EventAction *out) const
    {
        struct Map
        {
            const char *factory;
            const char *direct;
            ActionType type;
            int strings;
            int dirMode;   // 0 - нет, 1 - обязательно, 2 - необязательно
        };

        static const Map maps[] =
        {
            { "actionBuildRoute", "buildRoute", ActionType::BuildTrainRoute, 2, 1 },
            { "actionBuildTrainRoute", "buildTrainRoute", ActionType::BuildTrainRoute, 2, 1 },
            { "actionBuildShuntingRoute", "buildShuntingRoute", ActionType::BuildShuntingRoute, 2, 1 },
            { "actionSetSwitchsAlongRoute", "setSwitchsAlongRoute", ActionType::SetSwitchesAlongRoute, 2, 1 },
            { nullptr, "switchFwd", ActionType::SwitchFwd, 1, 0 },
            { nullptr, "switchBwd", ActionType::SwitchBwd, 1, 0 },
            { nullptr, "openSignal", ActionType::OpenSignal, 1, 2 },
            { "actionOpenShuntingSignal", "openShuntingSignal", ActionType::OpenShuntingSignal, 1, 1 },
            { "actionOpenCallSignal", "openCallSignal", ActionType::OpenCallSignal, 1, 1 },
            { "actionCloseSignal", "closeSignal", ActionType::CloseSignal, 1, 2 },
            { "actionRenameTrain", "renameTrain", ActionType::RenameTrain, 2, 0 },
            { "actionReverseTrain", "reverseTrain", ActionType::ReverseTrain, 1, 0 }
        };

        for (const Map &m : maps)
        {
            const char *want = direct ? m.direct : m.factory;

            if (want == nullptr || name != QLatin1String(want))
            {
                continue;
            }

            const int min_args = m.strings + (m.dirMode == 1 ? 1 : 0);
            const int max_args = m.strings + (m.dirMode != 0 ? 1 : 0);

            if (args.size() < min_args || args.size() > max_args)
            {
                return false;
            }

            EventAction a;
            a.type = m.type;

            if (!str(args[0], &a.a))
            {
                return false;
            }

            if (m.strings > 1 && !str(args[1], &a.b))
            {
                return false;
            }

            a.dir = (m.dirMode == 0) ? 1 : 0;

            if (m.dirMode != 0 && args.size() > m.strings)
            {
                if (!intval(args[m.strings], &a.dir) || (a.dir != 1 && a.dir != -1))
                {
                    return false;
                }
            }

            *out = a;

            return true;
        }

        return false;
    }
};

QString trimBlock(const QString &s)
{
    return s.trimmed();
}

int parseClock(const QString &text, bool *ok)
{
    static const QRegularExpression re(QStringLiteral("^(\\d{1,2}):(\\d{2})(?::(\\d{2}))?$"));
    const QRegularExpressionMatch m = re.match(text.trimmed());

    if (!m.hasMatch())
    {
        *ok = false;
        return 0;
    }

    const int h = m.captured(1).toInt();
    const int mi = m.captured(2).toInt();
    const int s = m.captured(3).isEmpty() ? 0 : m.captured(3).toInt();

    *ok = (h < 24 && mi < 60 && s < 60);

    return h * 3600 + mi * 60 + s;
}

bool containsIdent(const QString &text, const QString &name)
{
    const QRegularExpression re(QStringLiteral("(?<![A-Za-z0-9_])") + QRegularExpression::escape(name)
                                + QStringLiteral("(?![A-Za-z0-9_])"));

    return re.match(text).hasMatch();
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ImportResult importLua(const Route &route, const VehicleCatalog &catalog, const QString &lua_text)
{
    Q_UNUSED(route);

    ImportResult res;

    // Триггеры, созданные самим редактором по графикам, не часть сценария пользователя
    QString text = lua_text;
    const int a = text.indexOf(QStringLiteral("-- [rse-auto begin]"));
    const int b = text.indexOf(QStringLiteral("-- [rse-auto end]"));

    if (a >= 0 && b > a)
    {
        text.remove(a, b + QStringLiteral("-- [rse-auto end]").size() - a);
    }

    const Lexed L = lex(text);

    if (!L.ok)
    {
        res.error = QObject::tr("main.lua не разобран: незакрытая строка или комментарий.");
        return res;
    }

    Parser P(L);
    const QVector<Tok> &T = L.toks;
    const QVector<Range> stmts = splitStatements(T, 0, T.size());

    QHash<QString, FuncRec> funcs;
    QStringList pre;
    QStringList post;
    QVector<ScenarioEvent> events;
    QVector<QPair<int, QString>> func_blocks;   // индекс в pre и имя - для удаления поглощённых
    QVector<QPair<int, QString>> const_blocks;  // то же для констант

    int start_time = -1;
    int prev_end_char = 0;
    int recognized_trains = 0;

    auto source_of = [&](const Range &r)
    {
        return L.src.mid(T[r.a].start, T[r.b - 1].end - T[r.a].start);
    };

    for (const Range &r : stmts)
    {
        const int char_start = T[r.a].start;
        const int char_end = T[r.b - 1].end;

        // Комментарии между операторами: берём как заголовок/хвост блока
        const QString lead = L.src.mid(prev_end_char, char_start - prev_end_char).trimmed();
        prev_end_char = char_end;

        const QString raw_with_comments = (lead.isEmpty() ? QString() : lead + QLatin1Char('\n'))
                                          + L.src.mid(char_start, char_end - char_start);

        auto keep = [&](bool as_pre)
        {
            (as_pre ? pre : post) << trimBlock(raw_with_comments);
        };

        QString title;

        if (!lead.isEmpty())
        {
            // первая строка комментария без «--»
            QString first = lead.split(QLatin1Char('\n')).first().trimmed();

            if (first.startsWith(QLatin1String("--")))
            {
                first = first.mid(2).trimmed();

                if (!first.startsWith(QLatin1Char('[')))
                {
                    title = first;
                }
            }
        }

        const Tok &t0 = T[r.a];

        // function name() ... end
        if (isIdent(t0, "function"))
        {
            FuncRec f;

            if (r.b - r.a >= 5 && T[r.a + 1].kind == TK::Ident && isPunct(T[r.a + 2], "(")
                && isPunct(T[r.a + 3], ")") && isIdent(T[r.b - 1], "end"))
            {
                f.name = T[r.a + 1].text;
                f.raw = trimBlock(raw_with_comments);

                // тело: операторы между ")" и "end"
                bool ok = true;
                const QVector<Range> body = splitStatements(T, r.a + 4, r.b - 1);

                if (body.isEmpty())
                {
                    ok = false;
                }

                for (const Range &br : body)
                {
                    QString name;
                    QVector<Range> args;

                    if (!P.parseCall(br.a, br.b, &name, &args))
                    {
                        ok = false;
                        break;
                    }

                    if (name == QLatin1String("setPostEventTimeTrigger") && body.size() == 1 && args.size() == 2)
                    {
                        QString rel;
                        bool tok = false;

                        if (P.str(args[0], &rel) && rel.startsWith(QLatin1Char('+')))
                        {
                            const int sec = parseClock(rel.mid(1), &tok);
                            QString an;
                            QVector<Range> aa;

                            if (tok && args[1].b - args[1].a >= 1)
                            {
                                EventAction act;

                                if (args[1].b - args[1].a == 1 && T[args[1].a].kind == TK::Ident
                                    && funcs.contains(T[args[1].a].text) && funcs[T[args[1].a].text].convertible
                                    && funcs[T[args[1].a].text].delaySec < 0)
                                {
                                    f.actions = funcs[T[args[1].a].text].actions;
                                    funcs[T[args[1].a].text].consumed = true;
                                    f.delaySec = sec;
                                }
                                else if (P.parseCall(args[1].a, args[1].b, &an, &aa)
                                         && P.actionFromCall(an, aa, false, &act))
                                {
                                    f.actions = { act };
                                    f.delaySec = sec;
                                }
                                else
                                {
                                    ok = false;
                                }
                            }
                            else
                            {
                                ok = false;
                            }
                        }
                        else
                        {
                            ok = false;
                        }

                        break;
                    }

                    EventAction act;

                    if (!P.actionFromCall(name, args, true, &act))
                    {
                        ok = false;
                        break;
                    }

                    f.actions.push_back(act);
                }

                f.convertible = ok && !f.actions.isEmpty();
                f.opaqueIndex = pre.size();
                funcs.insert(f.name, f);
                func_blocks.push_back({ pre.size(), f.name });
            }

            keep(true);
            continue;
        }

        // [local] X = TrainData.new()
        {
            int a = r.a;

            if (isIdent(T[a], "local"))
            {
                ++a;
            }

            if (a + 6 <= r.b && T[a].kind == TK::Ident && isPunct(T[a + 1], "=")
                && isIdent(T[a + 2], "TrainData") && isPunct(T[a + 3], ".") && isIdent(T[a + 4], "new")
                && isPunct(T[a + 5], "(") && r.b - a == 7 && isPunct(T[a + 6], ")"))
            {
                TrainRec rec;
                rec.var = T[a].text;
                rec.raw << trimBlock(raw_with_comments);
                P.trains.insert(rec.var, rec);
                continue;
            }
        }

        // X.поле = значение
        if (r.b - r.a >= 5 && t0.kind == TK::Ident && isPunct(T[r.a + 1], ".") && T[r.a + 2].kind == TK::Ident
            && isPunct(T[r.a + 3], "=") && P.trains.contains(t0.text))
        {
            TrainRec &rec = P.trains[t0.text];
            const QString field = T[r.a + 2].text;
            const Range vr { r.a + 4, r.b };
            rec.raw << trimBlock(raw_with_comments);

            if (field == QLatin1String("name") && P.str(vr, &rec.name)) { rec.hasName = true; }
            else if (field == QLatin1String("config") && P.str(vr, &rec.config)) { rec.hasConfig = true; }
            else if (field == QLatin1String("traj") && P.str(vr, &rec.traj)) { rec.hasTraj = true; }
            else if (field == QLatin1String("coord"))
            {
                const auto v = P.eval(vr);

                if (v && v->kind == Value::Num) { rec.coord = v->n; } else { rec.ok = false; }
            }
            else if (field == QLatin1String("dir"))
            {
                int d = 0;

                if (P.intval(vr, &d) && (d == 1 || d == -1)) { rec.dir = d; } else { rec.ok = false; }
            }
            else if (field == QLatin1String("auto"))
            {
                const auto v = P.eval(vr);

                if (v && v->kind == Value::Bool) { rec.autoFlag = v->b; } else { rec.ok = false; }
            }
            else
            {
                rec.ok = false;
            }

            continue;
        }

        QString name;
        QVector<Range> args;
        const bool is_call = P.parseCall(r.a, r.b, &name, &args);

        // setTrain(X)
        if (is_call && name == QLatin1String("setTrain") && args.size() == 1 && args[0].b - args[0].a == 1
            && P.trains.contains(T[args[0].a].text))
        {
            TrainRec rec = P.trains[T[args[0].a].text];
            rec.raw << trimBlock(raw_with_comments);

            const TrainConfigData *cfg = catalog.findTrain(rec.config);
            bool ok = rec.ok && rec.hasName && rec.hasConfig && rec.hasTraj;

            if (ok && cfg == nullptr)
            {
                res.warnings << QObject::tr("Поезд «%1»: состав «%2» не найден в cfg/trains — поезд "
                                            "сохранён как код Lua без правки.").arg(rec.name, rec.config);
                ok = false;
            }

            if (ok && route.isLoaded() && route.find(rec.traj) == nullptr)
            {
                res.warnings << QObject::tr("Поезд «%1»: траектории «%2» нет в маршруте — поезд "
                                            "сохранён как код Lua без правки.").arg(rec.name, rec.traj);
                ok = false;
            }

            if (ok)
            {
                PlacedTrain t;

                t.name = rec.name;
                t.luaVar = rec.var;
                t.title = cfg->title.isEmpty() ? cfg->name : cfg->title;
                t.sourceTrain = cfg->name;
                t.groups = cfg->groups;
                t.trajectory = rec.traj;
                t.coord = rec.coord;
                t.dir = rec.dir;
                t.autoMode = rec.autoFlag;
                updateSummary(t, catalog);
                res.scenario.add(t);
                ++recognized_trains;
            }
            else
            {
                for (const QString &s : rec.raw)
                {
                    pre << s;
                }

                // переменная нужна, пока на неё ссылаются: константы из полей не подставляем
                P.trains[rec.var].ok = false;
            }

            continue;
        }

        // setTime("...")
        if (is_call && name == QLatin1String("setTime") && args.size() == 1)
        {
            QString tm;
            bool ok = false;

            if (P.str(args[0], &tm))
            {
                const int sec = parseClock(tm, &ok);

                if (ok && start_time < 0)
                {
                    start_time = sec;
                    continue;
                }
            }

            keep(false);
            continue;
        }

        // Константы: [local] имя = литерал
        {
            int a = r.a;

            if (isIdent(T[a], "local"))
            {
                ++a;
            }

            if (a + 2 < r.b && T[a].kind == TK::Ident && isPunct(T[a + 1], "=") && !isPunct(T[a + 2], "="))
            {
                const auto v = P.eval({ a + 2, r.b });

                if (v)
                {
                    P.consts.insert(T[a].text, *v);
                    const_blocks.push_back({ pre.size(), T[a].text });
                    keep(true);
                    continue;
                }

                keep(false);
                continue;
            }
        }

        // События
        if (is_call)
        {
            ScenarioEvent e;
            e.title = title;

            bool converted = false;

            auto handler = [&](Range hr, ScenarioEvent *ev) -> bool
            {
                QString an;
                QVector<Range> aa;
                EventAction act;

                if (hr.b - hr.a == 1 && T[hr.a].kind == TK::Ident && funcs.contains(T[hr.a].text))
                {
                    FuncRec &f = funcs[T[hr.a].text];

                    if (!f.convertible)
                    {
                        return false;
                    }

                    if (f.delaySec >= 0)
                    {
                        ev->delaySec = f.delaySec;
                    }

                    ev->actions = f.actions;
                    f.consumed = true;

                    return true;
                }

                if (P.parseCall(hr.a, hr.b, &an, &aa) && P.actionFromCall(an, aa, false, &act))
                {
                    ev->actions = { act };

                    return true;
                }

                return false;
            };

            if (name == QLatin1String("setTimeTrigger") && args.size() == 2)
            {
                QString tm;
                bool ok = false;

                if (P.str(args[0], &tm))
                {
                    if (tm.startsWith(QLatin1Char('+')))
                    {
                        e.trigger = TriggerType::AfterStart;
                        e.time = parseClock(tm.mid(1), &ok);
                    }
                    else
                    {
                        e.trigger = TriggerType::AtTime;
                        e.time = parseClock(tm, &ok);
                    }

                    if (ok)
                    {
                        // функция с задержкой для абсолютного времени не поддерживается
                        ScenarioEvent tmp;

                        if (handler(args[1], &tmp))
                        {
                            if (tmp.delaySec == 0)
                            {
                                e.actions = tmp.actions;
                                converted = true;
                            }
                            else if (args[1].b - args[1].a == 1 && funcs.contains(T[args[1].a].text))
                            {
                                funcs[T[args[1].a].text].consumed = false;
                            }
                        }
                    }
                }
            }
            else if ((name == QLatin1String("setOnTrajBusyTrigger") || name == QLatin1String("setOnTrajFreeTrigger"))
                     && args.size() == 2)
            {
                e.trigger = (name == QLatin1String("setOnTrajBusyTrigger")) ? TriggerType::TrajBusy
                                                                             : TriggerType::TrajFree;

                if (P.str(args[0], &e.traj) && handler(args[1], &e))
                {
                    converted = true;
                }
            }
            else if ((name == QLatin1String("setOnTrajBusyByTrainTrigger")
                      || name == QLatin1String("setOnTrajFreeByTrainTrigger")) && args.size() == 3)
            {
                e.trigger = (name == QLatin1String("setOnTrajBusyByTrainTrigger")) ? TriggerType::TrajBusy
                                                                                    : TriggerType::TrajFree;

                if (P.str(args[0], &e.traj) && P.str(args[1], &e.train) && handler(args[2], &e))
                {
                    converted = true;
                }
            }
            else if (name == QLatin1String("setAutoApproachTrigger") && args.size() == 3)
            {
                e.trigger = TriggerType::AutoApproach;

                if (P.str(args[0], &e.traj) && P.str(args[1], &e.traj2) && P.intval(args[2], &e.dir)
                    && (e.dir == 1 || e.dir == -1))
                {
                    converted = true;
                }
            }

            if (converted)
            {
                if (e.title.isEmpty())
                {
                    e.title = QObject::tr("Событие %1").arg(events.size() + 1);
                }

                events.push_back(e);
                continue;
            }
        }

        keep(false);
    }

    // Функции, поглощённые событиями, нужны только если на них ссылается оставшийся код
    QString rest = pre.join(QLatin1Char('\n')) + QLatin1Char('\n') + post.join(QLatin1Char('\n'));
    QSet<int> drop;

    for (const auto &fb : func_blocks)
    {
        const FuncRec &f = funcs[fb.second];

        if (!f.consumed)
        {
            continue;
        }

        // ссылки в остальном коде, кроме самого объявления
        QString others = rest;
        others.remove(pre[fb.first]);

        if (!containsIdent(others, f.name))
        {
            drop.insert(fb.first);
        }
    }

    // Константы, на которые никто больше не ссылается (события уже содержат значения)
    for (const auto &cb : const_blocks)
    {
        QString others = rest;
        others.remove(pre[cb.first]);

        if (!containsIdent(others, cb.second))
        {
            drop.insert(cb.first);
        }
    }

    QStringList pre_kept;

    for (int i = 0; i < pre.size(); ++i)
    {
        if (!drop.contains(i))
        {
            pre_kept << pre[i];
        }
    }

    res.info.opaquePre = pre_kept;
    res.info.opaquePost = post;
    res.info.events = events;
    res.info.startTime = start_time;

    if (start_time < 0)
    {
        res.warnings << QObject::tr("В main.lua нет setTime — при экспорте время начала будет выбрано автоматически.");
    }

    res.trainsRecognized = recognized_trains;
    res.eventsRecognized = events.size();
    res.opaqueBlocks = pre_kept.size() + post.size();

    res.notes << QObject::tr("Поездов распознано: %1; событий: %2; блоков Lua сохранено как есть: %3.")
                 .arg(res.trainsRecognized).arg(res.eventsRecognized).arg(res.opaqueBlocks);

    res.ok = true;

    return res;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
namespace
{

int timeFromText(const QString &text, bool *ok)
{
    return parseClock(text, ok);
}

bool readTimetable(const QString &path, Timetable *tt, QString *error)
{
    QFile f(path);

    if (!f.open(QIODevice::ReadOnly))
    {
        *error = QObject::tr("Не удалось открыть %1").arg(path);
        return false;
    }

    QXmlStreamReader xml(&f);

    TimetableStation cur;
    bool in_station = false;
    QString arrival;
    QString departure;
    QString field;

    while (!xml.atEnd())
    {
        xml.readNext();

        if (xml.isStartElement())
        {
            if (xml.name() == QLatin1String("Station"))
            {
                cur = TimetableStation();
                arrival.clear();
                departure.clear();
                in_station = true;
            }
            else if (in_station)
            {
                field = xml.name().toString();

                const QString v = xml.readElementText().trimmed();

                if (field == QLatin1String("Name")) cur.name = v;
                else if (field == QLatin1String("ArrivalTime")) arrival = v;
                else if (field == QLatin1String("DepartureTime")) departure = v;
                else if (field == QLatin1String("TargetTraj")) cur.targetTrajectory = v;
                else if (field == QLatin1String("TargetCoord")) cur.targetCoord = v.toDouble();
                else if (field == QLatin1String("ApproachTraj")) cur.approachTrajectory = v;
                else if (field == QLatin1String("RemovalTraj")) cur.removalTrajectory = v;
                else if (field == QLatin1String("IsRightPlatform")) cur.rightPlatform = (v == "1" || v == "true");
                else if (field == QLatin1String("IsLeftPlatform")) cur.leftPlatform = (v == "1" || v == "true");
                else if (field == QLatin1String("IsVisible")) cur.visible = (v == "1" || v == "true");
            }
        }
        else if (xml.isEndElement() && xml.name() == QLatin1String("Station") && in_station)
        {
            bool ok_a = false;
            bool ok_d = false;
            const int a = (arrival.isEmpty() || arrival == QLatin1String("-")) ? 0 : timeFromText(arrival, &ok_a);
            const int d = (departure.isEmpty() || departure == QLatin1String("-")) ? 0 : timeFromText(departure, &ok_d);

            cur.arrival = ok_a ? a : (ok_d ? d : 0);
            cur.departure = ok_d ? d : cur.arrival;

            if (cur.departure < cur.arrival)
            {
                cur.departure += 86400;
            }

            tt->stations.push_back(cur);
            in_station = false;
        }
    }

    if (xml.hasError())
    {
        *error = QObject::tr("%1: %2").arg(path, xml.errorString());
        return false;
    }

    if (tt->stations.size() < 2)
    {
        *error = QObject::tr("В расписании %1 меньше двух станций").arg(path);
        return false;
    }

    // Время идёт вперёд: переход через полночь
    for (int i = 1; i < tt->stations.size(); ++i)
    {
        TimetableStation &s = tt->stations[i];
        const int prev = tt->stations[i - 1].departure;

        while (s.arrival < prev)
        {
            s.arrival += 86400;
            s.departure += 86400;
        }
    }

    tt->valid = true;
    tt->startTime = tt->stations.first().departure;
    tt->endTrajectory = tt->stations.last().targetTrajectory;
    tt->endCoord = tt->stations.last().targetCoord;

    return true;
}

} // namespace

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
ImportResult importScenario(const Route &route, const VehicleCatalog &catalog,
                            const QString &scenario_dir)
{
    ImportResult res;

    const QDir dir(scenario_dir);
    QFile f(dir.filePath(QStringLiteral("main.lua")));

    if (!f.open(QIODevice::ReadOnly))
    {
        res.error = QObject::tr("В каталоге %1 нет файла main.lua").arg(scenario_dir);
        return res;
    }

    res = importLua(route, catalog, QString::fromUtf8(f.readAll()));

    if (!res.ok)
    {
        return res;
    }

    res.info.scenarioName = QFileInfo(scenario_dir).fileName();

    // Расписания
    for (int i = 0; i < res.scenario.size(); ++i)
    {
        PlacedTrain t = res.scenario.trains()[i];
        const QString path = dir.filePath(QStringLiteral("timetable/%1.xml").arg(t.name));

        if (!QFileInfo::exists(path))
        {
            continue;
        }

        Timetable tt;
        QString err;

        if (readTimetable(path, &tt, &err))
        {
            t.timetable = tt;
            res.scenario.replace(i, t);
            res.notes << QObject::tr("Расписание поезда «%1»: станций %2.").arg(t.name).arg(tt.stations.size());
        }
        else
        {
            res.warnings << err;
        }
    }

    // Расписания без поездов, которые редактор не разобрал, остаются на диске
    // (при экспорте с заменой файлы расписаний распознанных поездов перезаписываются)

    QFile readme(dir.filePath(QStringLiteral("README.md")));

    if (readme.open(QIODevice::ReadOnly))
    {
        QString text = QString::fromUtf8(readme.readAll());

        // Отрезаем разделы, которые редактор дописывает сам
        const int gen = text.indexOf(QRegularExpression(QStringLiteral("^Маршрут: .*Начало игры: .*$"),
                                                        QRegularExpression::MultilineOption));

        if (gen >= 0 && text.contains(QStringLiteral("### Поезда")))
        {
            text = text.left(gen);
        }

        QStringList lines = text.split(QLatin1Char('\n'));

        if (!lines.isEmpty() && lines.first().startsWith(QStringLiteral("## ")) && text.contains(QStringLiteral("### Поезда")))
        {
            lines.removeFirst();
        }

        res.info.description = lines.join(QLatin1Char('\n')).trimmed();
    }

    // Сценарий лежит в каталоге другого маршрута
    if (route.isLoaded())
    {
        const QString expected = QDir(route.dir()).absoluteFilePath(QStringLiteral("scenarios"));

        if (QDir::cleanPath(QFileInfo(scenario_dir).absolutePath()) != QDir::cleanPath(expected))
        {
            res.warnings << QObject::tr("Каталог сценария не в scenarios/ открытого маршрута: при экспорте "
                                        "сценарий будет записан в каталог маршрута.");
        }
    }

    return res;
}

} // namespace rse
