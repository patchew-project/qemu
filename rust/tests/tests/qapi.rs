// SPDX-License-Identifier: GPL-2.0-or-later

#![allow(unexpected_cfgs)]
#![allow(clippy::shadow_unrelated)]

use util::qobject::{from_qobject, to_qobject, QObject};

#[test]
fn test_char() {
    let json = "\"v\"";
    let qo = QObject::from_json(json).unwrap();
    let c: char = from_qobject(qo).unwrap();
    assert_eq!(c, 'v');
    assert_eq!(to_qobject(c).unwrap().to_json(), json);

    let json = "'va'";
    let qo = QObject::from_json(json).unwrap();
    from_qobject::<char>(qo).unwrap_err();
}

#[test]
fn test_enum() {
    let json = "\"value1\"";
    let qo = QObject::from_json(json).unwrap();
    let e: test_qapi::EnumOne = from_qobject(qo).unwrap();
    assert_eq!(e, test_qapi::EnumOne::VALUE1);
    assert_eq!(to_qobject(e).unwrap().to_json(), json);
}

#[test]
fn test_struct() {
    let expected = test_qapi::TestStruct {
        integer: -42,
        boolean: true,
        string: "foo".into(),
    };
    let json = "{\"integer\": -42, \"boolean\": true, \"string\": \"foo\"}";
    let qo = QObject::from_json(json).unwrap();
    let ts: test_qapi::TestStruct = from_qobject(qo).unwrap();
    assert_eq!(ts, expected);
    assert_eq!(to_qobject(ts).unwrap().to_json(), json);
}

#[test]
fn test_struct_nested() {
    let expected = test_qapi::UserDefTwo {
        string0: "string0".into(),
        dict1: test_qapi::UserDefTwoDict {
            string1: "string1".into(),
            dict2: test_qapi::UserDefTwoDictDict {
                userdef: test_qapi::UserDefOne {
                    integer: 42,
                    string: "string".into(),
                    enum1: None,
                },
                string: "string2".into(),
            },
            dict3: None,
        },
    };
    let json = "{\"string0\": \"string0\", \"dict1\": {\"dict2\": {\"string\": \"string2\", \
                \"userdef\": {\"integer\": 42, \"string\": \"string\"}}, \"string1\": \
                \"string1\"}}";
    let qo = QObject::from_json(json).unwrap();
    let udt: test_qapi::UserDefTwo = from_qobject(qo).unwrap();
    assert_eq!(udt, expected);
    assert_eq!(to_qobject(udt).unwrap().to_json(), json);
}

#[test]
fn test_list() {
    let expected = [
        test_qapi::UserDefOne {
            integer: 42,
            string: "string0".into(),
            enum1: None,
        },
        test_qapi::UserDefOne {
            integer: 43,
            string: "string1".into(),
            enum1: None,
        },
        test_qapi::UserDefOne {
            integer: 44,
            string: "string2".into(),
            enum1: None,
        },
    ];
    let json = "[{\"integer\": 42, \"string\": \"string0\"}, {\"integer\": 43, \"string\": \
                \"string1\"}, {\"integer\": 44, \"string\": \"string2\"}]";
    let qo = QObject::from_json(json).unwrap();
    let ud_list: Vec<test_qapi::UserDefOne> = from_qobject(qo).unwrap();
    assert_eq!(ud_list, expected);
    assert_eq!(to_qobject(ud_list).unwrap().to_json(), json);
}

#[test]
fn test_flat_union() {
    let expected = test_qapi::UserDefFlatUnion {
        integer: 41,
        string: "str".into(),
        u: test_qapi::UserDefFlatUnionVariant::Value1(test_qapi::UserDefA {
            boolean: true,
            a_b: None,
        }),
    };
    let json = "{\"integer\": 41, \"boolean\": true, \"enum1\": \"value1\", \"string\": \"str\"}";
    let qo = QObject::from_json(json).unwrap();
    let ud_fu: test_qapi::UserDefFlatUnion = from_qobject(qo).unwrap();
    assert_eq!(ud_fu, expected);
    assert_eq!(to_qobject(ud_fu).unwrap().to_json(), json);
}

#[test]
fn test_union_in_union() {
    let expected = test_qapi::TestUnionInUnion {
        u: test_qapi::TestUnionInUnionVariant::ValueA(test_qapi::TestUnionTypeA {
            u: test_qapi::TestUnionTypeAVariant::ValueA1(test_qapi::TestUnionTypeA1 {
                integer: 2,
                name: "fish".into(),
            }),
        }),
    };
    let json =
        "{\"name\": \"fish\", \"integer\": 2, \"type-a\": \"value-a1\", \"type\": \"value-a\"}";
    let qo = QObject::from_json(json).unwrap();
    let uu: test_qapi::TestUnionInUnion = from_qobject(qo).unwrap();
    assert_eq!(uu, expected);
    assert_eq!(to_qobject(expected).unwrap().to_json(), json);

    let expected = test_qapi::TestUnionInUnion {
        u: test_qapi::TestUnionInUnionVariant::ValueA(test_qapi::TestUnionTypeA {
            u: test_qapi::TestUnionTypeAVariant::ValueA2(test_qapi::TestUnionTypeA2 {
                integer: 1729,
                size: 87539319,
            }),
        }),
    };
    let json =
        "{\"integer\": 1729, \"type-a\": \"value-a2\", \"size\": 87539319, \"type\": \"value-a\"}";
    let qo = QObject::from_json(json).unwrap();
    let uu: test_qapi::TestUnionInUnion = from_qobject(qo).unwrap();
    assert_eq!(uu, expected);
    assert_eq!(to_qobject(expected).unwrap().to_json(), json);

    let expected = test_qapi::TestUnionInUnion {
        u: test_qapi::TestUnionInUnionVariant::ValueB(test_qapi::TestUnionTypeB {
            integer: 1729,
            onoff: true,
        }),
    };
    let json = "{\"integer\": 1729, \"onoff\": true, \"type\": \"value-b\"}";
    let qo = QObject::from_json(json).unwrap();
    let uu: test_qapi::TestUnionInUnion = from_qobject(qo).unwrap();
    assert_eq!(uu, expected);
    assert_eq!(to_qobject(expected).unwrap().to_json(), json);
}

#[test]
fn test_alternate() {
    let expected = test_qapi::UserDefAlternate::I(42);
    let json = "42";
    let qo = QObject::from_json(json).unwrap();
    let uda: test_qapi::UserDefAlternate = from_qobject(qo).unwrap();
    assert_eq!(uda, expected);
    assert_eq!(to_qobject(expected).unwrap().to_json(), json);

    let expected = test_qapi::UserDefAlternate::E(test_qapi::EnumOne::VALUE1);
    let json = "\"value1\"";
    let qo = QObject::from_json(json).unwrap();
    let uda: test_qapi::UserDefAlternate = from_qobject(qo).unwrap();
    assert_eq!(uda, expected);
    assert_eq!(to_qobject(expected).unwrap().to_json(), json);

    let expected = test_qapi::UserDefAlternate::N(());
    let json = "null";
    let qo = QObject::from_json(json).unwrap();
    let uda: test_qapi::UserDefAlternate = from_qobject(qo).unwrap();
    assert_eq!(uda, expected);
    assert_eq!(to_qobject(expected).unwrap().to_json(), json);

    let expected = test_qapi::UserDefAlternate::Udfu(test_qapi::UserDefFlatUnion {
        integer: 42,
        string: "str".to_string(),
        u: test_qapi::UserDefFlatUnionVariant::Value1(test_qapi::UserDefA {
            boolean: true,
            a_b: None,
        }),
    });
    let json = "{\"integer\": 42, \"boolean\": true, \"enum1\": \"value1\", \"string\": \"str\"}";
    let qo = QObject::from_json(json).unwrap();
    let uda: test_qapi::UserDefAlternate = from_qobject(qo).unwrap();
    assert_eq!(uda, expected);
    assert_eq!(to_qobject(expected).unwrap().to_json(), json);

    let expected = test_qapi::WrapAlternate {
        alt: test_qapi::UserDefAlternate::I(42),
    };
    let json = "{\"alt\": 42}";
    let qo = QObject::from_json(json).unwrap();
    let uda: test_qapi::WrapAlternate = from_qobject(qo).unwrap();
    assert_eq!(uda, expected);
    assert_eq!(to_qobject(expected).unwrap().to_json(), json);

    let expected = test_qapi::WrapAlternate {
        alt: test_qapi::UserDefAlternate::E(test_qapi::EnumOne::VALUE1),
    };
    let json = "{\"alt\": \"value1\"}";
    let qo = QObject::from_json(json).unwrap();
    let uda: test_qapi::WrapAlternate = from_qobject(qo).unwrap();
    assert_eq!(uda, expected);
    assert_eq!(to_qobject(expected).unwrap().to_json(), json);

    let expected = test_qapi::WrapAlternate {
        alt: test_qapi::UserDefAlternate::Udfu(test_qapi::UserDefFlatUnion {
            integer: 1,
            string: "str".to_string(),
            u: test_qapi::UserDefFlatUnionVariant::Value1(test_qapi::UserDefA {
                boolean: true,
                a_b: None,
            }),
        }),
    };
    let json = "{\"alt\": {\"integer\": 1, \"boolean\": true, \"enum1\": \"value1\", \"string\": \
                \"str\"}}";
    let qo = QObject::from_json(json).unwrap();
    let uda: test_qapi::WrapAlternate = from_qobject(qo).unwrap();
    assert_eq!(uda, expected);
    assert_eq!(to_qobject(expected).unwrap().to_json(), json);
}

#[test]
fn test_alternate_number() {
    let expected = test_qapi::AltEnumNum::N(42.0);
    let json = "42";
    let qo = QObject::from_json(json).unwrap();
    let uda: test_qapi::AltEnumNum = from_qobject(qo).unwrap();
    assert_eq!(uda, expected);
    assert_eq!(to_qobject(expected).unwrap().to_json(), json);

    let expected = test_qapi::AltNumEnum::N(42.0);
    let json = "42";
    let qo = QObject::from_json(json).unwrap();
    let uda: test_qapi::AltNumEnum = from_qobject(qo).unwrap();
    assert_eq!(uda, expected);
    assert_eq!(to_qobject(expected).unwrap().to_json(), json);

    let expected = test_qapi::AltEnumInt::I(42);
    let json = "42";
    let qo = QObject::from_json(json).unwrap();
    let uda: test_qapi::AltEnumInt = from_qobject(qo).unwrap();
    assert_eq!(uda, expected);
    assert_eq!(to_qobject(expected).unwrap().to_json(), json);

    let expected = test_qapi::AltListInt::I(42);
    let json = "42";
    let qo = QObject::from_json(json).unwrap();
    let uda: test_qapi::AltListInt = from_qobject(qo).unwrap();
    assert_eq!(&uda, &expected);
    assert_eq!(to_qobject(&expected).unwrap().to_json(), json);

    // double
    let json = "42.5";
    let qo = QObject::from_json(json).unwrap();
    from_qobject::<test_qapi::AltEnumBool>(qo).unwrap_err();

    let expected = test_qapi::AltEnumNum::N(42.5);
    let json = "42.5";
    let qo = QObject::from_json(json).unwrap();
    let uda: test_qapi::AltEnumNum = from_qobject(qo).unwrap();
    assert_eq!(uda, expected);
    assert_eq!(to_qobject(expected).unwrap().to_json(), json);

    let expected = test_qapi::AltNumEnum::N(42.5);
    let json = "42.5";
    let qo = QObject::from_json(json).unwrap();
    let uda: test_qapi::AltNumEnum = from_qobject(qo).unwrap();
    assert_eq!(uda, expected);
    assert_eq!(to_qobject(expected).unwrap().to_json(), json);

    let json = "42.5";
    let qo = QObject::from_json(json).unwrap();
    from_qobject::<test_qapi::AltEnumInt>(qo).unwrap_err();
}

#[test]
fn test_alternate_list() {
    let expected = test_qapi::AltListInt::L(vec![42, 43, 44]);
    let json = "[42, 43, 44]";
    let qo = QObject::from_json(json).unwrap();
    let uda: test_qapi::AltListInt = from_qobject(qo).unwrap();
    assert_eq!(uda, expected);
    assert_eq!(to_qobject(expected).unwrap().to_json(), json);
}

#[test]
fn test_errors() {
    let json = "{ 'integer': false, 'boolean': 'foo', 'string': -42 }";
    let qo = QObject::from_json(json).unwrap();
    from_qobject::<test_qapi::TestStruct>(qo).unwrap_err();

    let json = "[ '1', '2', false, '3' ]";
    let qo = QObject::from_json(json).unwrap();
    from_qobject::<Vec<String>>(qo).unwrap_err();

    let json = "{ 'str': 'hi' }";
    let qo = QObject::from_json(json).unwrap();
    from_qobject::<test_qapi::UserDefTwo>(qo).unwrap_err();

    let json = "{}";
    let qo = QObject::from_json(json).unwrap();
    from_qobject::<test_qapi::WrapAlternate>(qo).unwrap_err();
}

#[test]
fn test_wrong_type() {
    let json = "[]";
    let qo = QObject::from_json(json).unwrap();
    from_qobject::<test_qapi::TestStruct>(qo).unwrap_err();

    let json = "{}";
    let qo = QObject::from_json(json).unwrap();
    from_qobject::<Vec<String>>(qo).unwrap_err();

    let json = "1";
    let qo = QObject::from_json(json).unwrap();
    from_qobject::<test_qapi::TestStruct>(qo).unwrap_err();

    let json = "{}";
    let qo = QObject::from_json(json).unwrap();
    from_qobject::<i64>(qo).unwrap_err();

    let json = "1";
    let qo = QObject::from_json(json).unwrap();
    from_qobject::<Vec<String>>(qo).unwrap_err();

    let json = "[]";
    let qo = QObject::from_json(json).unwrap();
    from_qobject::<i64>(qo).unwrap_err();
}

#[test]
fn test_fail_struct() {
    let json = "{ 'integer': -42, 'boolean': true, 'string': 'foo', 'extra': 42 }";
    let qo = QObject::from_json(json).unwrap();
    from_qobject::<test_qapi::TestStruct>(qo).unwrap_err();
}

#[test]
fn test_fail_struct_nested() {
    let json = "{ 'string0': 'string0', 'dict1': { 'string1': 'string1', 'dict2': { 'userdef1': { \
                'integer': 42, 'string': 'string', 'extra': [42, 23, {'foo':'bar'}] }, 'string2': \
                'string2'}}}";
    let qo = QObject::from_json(json).unwrap();
    from_qobject::<test_qapi::UserDefTwo>(qo).unwrap_err();
}

#[test]
fn test_fail_struct_in_list() {
    let json = "[ { 'string': 'string0', 'integer': 42 }, { 'string': 'string1', 'integer': 43 }, \
                { 'string': 'string2', 'integer': 44, 'extra': 'ggg' } ]";
    let qo = QObject::from_json(json).unwrap();
    from_qobject::<Vec<test_qapi::UserDefOne>>(qo).unwrap_err();
}

#[test]
fn test_fail_union_flat() {
    let json = "{ 'enum1': 'value2', 'string': 'c', 'integer': 41, 'boolean': true }";
    let qo = QObject::from_json(json).unwrap();
    from_qobject::<Vec<test_qapi::UserDefFlatUnion>>(qo).unwrap_err();
}

#[test]
fn test_fail_union_flat_no_discrim() {
    // test situation where discriminator field ('enum1' here) is missing
    let json = "{ 'integer': 42, 'string': 'c', 'string1': 'd', 'string2': 'e' }";
    let qo = QObject::from_json(json).unwrap();
    from_qobject::<Vec<test_qapi::UserDefFlatUnion2>>(qo).unwrap_err();
}

#[test]
fn test_fail_alternate() {
    let json = "3.14";
    let qo = QObject::from_json(json).unwrap();
    from_qobject::<Vec<test_qapi::UserDefAlternate>>(qo).unwrap_err();
}

#[test]
fn test_qapi() {
    let expected = qapi::InetSocketAddress {
        host: "host-val".to_string(),
        port: "port-val".to_string(),
        numeric: None,
        to: None,
        ipv4: None,
        ipv6: None,
        keep_alive: None,
        #[cfg(HAVE_TCP_KEEPCNT)]
        keep_alive_count: None,
        #[cfg(HAVE_TCP_KEEPIDLE)]
        keep_alive_idle: Some(42),
        #[cfg(HAVE_TCP_KEEPINTVL)]
        keep_alive_interval: None,
        #[cfg(HAVE_IPPROTO_MPTCP)]
        mptcp: None,
    };

    let qsa = to_qobject(&expected).unwrap();
    let json = qsa.to_json();
    assert_eq!(
        json,
        "{\"port\": \"port-val\", \"keep_alive_idle\": 42, \"host\": \"host-val\"}"
    );
    let sa: qapi::InetSocketAddress = from_qobject(qsa).unwrap();
    assert_eq!(sa, expected);

    let expected = qapi::SocketAddressVariant::Inet(expected);
    let qsav = to_qobject(&expected).unwrap();
    let json = qsav.to_json();
    assert_eq!(
        json,
        "{\"port\": \"port-val\", \"keep_alive_idle\": 42, \"host\": \"host-val\", \"type\": \
         \"inet\"}"
    );
    let sav: qapi::SocketAddressVariant = from_qobject(qsav).unwrap();
    assert_eq!(sav, expected);

    let expected = qapi::Qcow2BitmapInfo {
        name: "name-val".to_string(),
        granularity: 4096,
        flags: vec![
            qapi::Qcow2BitmapInfoFlags::IN_USE,
            qapi::Qcow2BitmapInfoFlags::AUTO,
        ],
    };
    let qbi = to_qobject(&expected).unwrap();
    let json = qbi.to_json();
    assert_eq!(
        json,
        "{\"flags\": [\"in-use\", \"auto\"], \"name\": \"name-val\", \"granularity\": 4096}"
    );
    let bi: qapi::Qcow2BitmapInfo = from_qobject(qbi).unwrap();
    assert_eq!(bi, expected);
}
