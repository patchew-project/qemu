#
# QAPI helper library
# C ABI verification test generator
#
# pylint: disable=too-few-public-methods

from typing import List, Optional

from .common import c_enum_const, c_name, mcgen
from .rs import rs_name, to_snake_case
from .schema import (
    QAPISchemaEnumMember,
    QAPISchemaIfCond,
    QAPISchemaObjectType,
    QAPISchemaObjectTypeMember,
    QAPISchemaVariants,
)


class CABI:
    def __init__(self, name: str, ifcond: QAPISchemaIfCond):
        self.name = name
        self.ifcond = ifcond

    def gen_c(self) -> str:
        raise NotImplementedError()

    def gen_rs(self) -> str:
        raise NotImplementedError()


class CABIEnum(CABI):
    def __init__(
        self,
        name: str,
        ifcond: QAPISchemaIfCond,
        members: List[QAPISchemaEnumMember],
        prefix: Optional[str] = None,
    ):
        super().__init__(name, ifcond)
        self.members = members
        self.prefix = prefix

    def gen_c(self) -> str:
        last = c_enum_const(self.name, "_MAX", self.prefix)
        ret = self.ifcond.gen_if()
        ret += mcgen("""
    printf("%(name)s enum: sizeof=%%zu\\n", sizeof(%(cname)s));
    printf(" max=%%d\\n", %(last)s);
    printf("\\n");
""", name=self.name, cname=c_name(self.name), last=last)
        ret += self.ifcond.gen_endif()
        return ret

    def gen_rs(self) -> str:
        return mcgen("""
%(cfg)s
{
    println!("%(name)s enum: sizeof={}", ::std::mem::size_of::<%(rs_name)s>());
    println!(" max={}", %(rs_name)s::_MAX as u32);
    println!();
}
""",
                     name=self.name,
                     rs_name=rs_name(self.name),
                     cfg=self.ifcond.rsgen())


class CABIStruct(CABI):
    def __init__(self, name: str, ifcond: QAPISchemaIfCond):
        super().__init__(name, ifcond)
        self.members: List[CABIStructMember] = []

    def add_members(self, members: List[QAPISchemaObjectTypeMember]) -> None:
        for memb in members:
            if memb.optional:
                self.add_member(memb.name, memb.ifcond, "has_")
            self.add_member(memb.name, memb.ifcond)

    def add_variants(self, variants: QAPISchemaVariants) -> None:
        for var in variants.variants:
            if var.type.name == "q_empty":
                continue
            self.add_member(var.name, var.ifcond, "u.")

    def add_member(self, member: str,
                   ifcond: Optional[QAPISchemaIfCond] = None,
                   prefix: str = '') -> None:
        self.members.append(CABIStructMember(self, member, ifcond, prefix))

    def gen_c(self) -> str:
        ret = self.ifcond.gen_if()
        ret += mcgen("""
    printf("%(name)s struct: sizeof=%%zu\\n", sizeof(%(name)s));
""", name=self.name)
        for member in self.members:
            ret += member.gen_c()
        ret += mcgen("""
    printf("\\n");
""")
        ret += self.ifcond.gen_endif()
        return ret

    def gen_rs(self) -> str:
        ret = mcgen("""
%(cfg)s
{
    println!("%(name)s struct: sizeof={}",
        ::std::mem::size_of::<%(rs_name)s>());
""", name=self.name, rs_name=rs_name(self.name), cfg=self.ifcond.rsgen())
        for member in self.members:
            ret += member.gen_rs()
        ret += mcgen("""
    println!();
}
""")
        return ret


class CABIStructMember:
    def __init__(self, struct: CABIStruct, name: str,
                 ifcond: Optional[QAPISchemaIfCond] = None,
                 prefix: str = ''):
        self.struct = struct
        self.name = name
        self.ifcond = ifcond or QAPISchemaIfCond()
        self.prefix = prefix

    def gen_c(self) -> str:
        ret = self.ifcond.gen_if()
        cmember = self.prefix + c_name(self.name)
        ret += mcgen("""
    printf(" %(member)s member: sizeof=%%zu offset=%%zu\\n",
            G_SIZEOF_MEMBER(struct %(sname)s, %(cmember)s),
            offsetof(struct %(sname)s, %(cmember)s));
""", member=self.name, sname=self.struct.name, cmember=cmember)
        ret += self.ifcond.gen_endif()
        return ret

    def gen_rs(self) -> str:
        ret = ''
        if self.ifcond:
            ret += self.ifcond.rsgen()
        protect = not self.prefix or self.prefix[-1] == '.'
        rsmember = self.prefix + to_snake_case(rs_name(self.name, protect))
        ret += mcgen("""
unsafe {
    println!(" %(member)s member: sizeof={} offset={}",
        ::std::mem::size_of_val(
            &(*::std::ptr::null::<%(sname)s>()).%(rsmember)s
        ),
        &(*(::std::ptr::null::<%(sname)s>())).%(rsmember)s as *const _ as usize
    );
}
""", member=self.name, sname=rs_name(self.struct.name), rsmember=rsmember)
        return ret


def gen_object_cabi(
    name: str,
    ifcond: QAPISchemaIfCond,
    base: Optional[QAPISchemaObjectType],
    members: List[QAPISchemaObjectTypeMember],
    variants: Optional[QAPISchemaVariants],
) -> List[CABI]:
    if name == 'q_empty':
        return []
    ret = []
    for var in variants.variants if variants else ():
        obj = var.type
        if not isinstance(obj, QAPISchemaObjectType):
            continue
        ret.extend(
            gen_object_cabi(
                obj.name, obj.ifcond, obj.base, obj.local_members, obj.variants
            )
        )
    cabi = CABIStruct(c_name(name), ifcond)
    if base:
        cabi.add_members(base.members)
    cabi.add_members(members)
    if variants:
        cabi.add_variants(variants)
    if (not base or base.is_empty()) and not members and not variants:
        cabi.add_member('qapi_dummy_for_empty_struct')
    ret.append(cabi)
    return ret
