from .regexps import *
from .patching import *
from .utils import *
from .qom_macros import *

RE_SIMPLE_VALUE = OR(RE_IDENTIFIER, RE_STRING, RE_NUMBER)

RE_FUN_CALL = S(RE_IDENTIFIER, r'\s*\(\s*', RE_SIMPLE_VALUE, r'\s*\)')
RE_SIZEOF = S(r'sizeof\s*\(\s*', NAMED('sizeoftype', RE_TYPE), r'\s*\)')

RE_ADDRESS = S(r'&\s*', RE_IDENTIFIER)

RE_ARRAY_ITEM = S(r'{\s*', NAMED('arrayitem', M(RE_SIMPLE_VALUE, n='?')), r'\s*}\s*,?')
RE_ARRAY_CAST = S(r'\(\s*', RE_IDENTIFIER, r'\s*\[\s*\]\)')
RE_ARRAY_ITEMS = M(S(RE_ARRAY_ITEM, SP))
RE_ARRAY = S(M(RE_ARRAY_CAST, n='?'), r'\s*{\s*',
             NAMED('arrayitems', RE_ARRAY_ITEMS),
             r'}')

RE_MACRO_CONCAT = M(S(OR(RE_IDENTIFIER, RE_STRING), SP), n='{2,}')

# NOTE: this covers a very small subset of valid expressions

RE_EXPRESSION = OR(RE_FUN_CALL, RE_SIMPLE_VALUE, RE_ARRAY, RE_SIZEOF,
                   RE_ADDRESS, RE_MACRO_CONCAT)

TI_FIELDS = [ 'name', 'parent', 'abstract', 'interfaces',
    'instance_size', 'instance_init', 'instance_post_init', 'instance_finalize',
    'class_size', 'class_init', 'class_base_init', 'class_data']

RE_TI_FIELD_NAME = OR(*TI_FIELDS)

RE_TI_FIELD_INIT = S(NAMED('comments', RE_COMMENTS),
                     r'\.', NAMED('field', RE_TI_FIELD_NAME), r'\s*=\s*',
                     NAMED('value', RE_EXPRESSION), r'\s*,?')
RE_TI_FIELDS = M(S(RE_TI_FIELD_INIT, SP))

RE_TYPEINFO_START = S(r'^[ \t]*', M(r'(static|const)\s+', name='modifiers'), r'TypeInfo\s+',
                      NAMED('name', RE_IDENTIFIER), r'\s*=\s*{')
RE_TYPEINFO_DEF = S(RE_TYPEINFO_START,
                    M(SP, NAMED('fields', RE_TI_FIELDS),
                      NAMED('endcomments', RE_COMMENTS),
                      r'^};?\n',
                      n='?', name='fullspec'))

ParsedArray = List[str]
ParsedInitializerValue = Union[str, ParsedArray]
class InitializerValue(NamedTuple):
    raw: str
    parsed: Optional[ParsedInitializerValue]
TypeInfoInitializers = Dict[str, InitializerValue]

def parse_array(m: Match) -> ParsedArray:
    #DBG('parse_array: %r', m.group(0))
    return [m.group('arrayitem') for m in re.finditer(RE_ARRAY_ITEM, m.group('arrayitems'))]

def parse_initializer_value(s) -> InitializerValue:
    parsed: Optional[ParsedInitializerValue] = None
    if m := re.match(RE_ARRAY, s):
        assert m is not None
        parsed = parse_array(m)
    return InitializerValue(s, parsed)

class TypeInfoVar(FileMatch):
    """TypeInfo variable declaration with initializer
    Will be replaced by OBJECT_DEFINE_TYPE* macro
    (not implemented yet)
    """
    regexp = RE_TYPEINFO_DEF

    @property
    def initializers(self) -> Optional[TypeInfoInitializers]:
        if getattr(self, '_inititalizers', None):
            self._initializers: TypeInfoInitializers
            return self._initializers
        fields = self.group('fields')
        if fields is None:
            return None
        d = dict((fm.group('field'), parse_initializer_value(fm.group('value')))
                  for fm in re.finditer(RE_TI_FIELD_INIT, fields))
        self._initializers = d
        return d

    def is_static(self) -> bool:
        return 'static' in self.group('modifiers')

    def is_full(self) -> bool:
        return bool(self.group('fullspec'))

    def get_initializers(self) -> TypeInfoInitializers:
        """Helper for code that needs to deal with missing initializer info"""
        if self.initializers is None:
            return {}
        return self.initializers

    def get_initializer_value(self, field: str) -> InitializerValue:
        return self.get_initializers().get(field, InitializerValue('', ''))

    def extract_identifiers(self) -> Optional[TypeIdentifiers]:
        """Try to extract identifiers from names being used"""
        DBG("extracting idenfiers from %s", self.name)
        values = self.initializers
        if values is None:
            return None
        if 'name' not in values:
            self.warn("name not set in TypeInfo variable %s", self.name)
            return None
        typename = values['name'].raw
        uppercase = None
        if typename and re.fullmatch(RE_IDENTIFIER, typename) and typename.startswith("TYPE_"):
            uppercase = typename[len('TYPE_'):]
        lowercase = None
        funcs = set()
        prefixes = set()
        for field,suffix in [('instance_init', '_init'),
                             ('instance_finalize', '_finalize'),
                             ('class_init', '_class_init')]:
            if field not in values:
                continue
            func = values[field].raw
            funcs.add(func)
            if func.endswith(suffix):
                prefixes.add(func[:-len(suffix)])
            else:
                self.warn("function name %s doesn't have expected %s suffix",
                          func, suffix)
        if len(prefixes) == 1:
            lowercase = prefixes.pop()
        elif len(prefixes) > 1:
            self.warn("inconsistent function names: %s", ' '.join(funcs))

        instancetype = None
        if 'instance_size' in values:
            m = re.fullmatch(RE_SIZEOF, values['instance_size'].raw)
            if m:
                instancetype = m.group('sizeoftype')
        classtype = None
        if 'class_size' in values:
            m = re.fullmatch(RE_SIZEOF, values['class_size'].raw)
            if m:
                classtype = m.group('sizeoftype')
        #.parent = TYPE_##PARENT_MODULE_OBJ_NAME, \
        return TypeIdentifiers(typename=typename,
                               uppercase=uppercase, lowercase=lowercase,
                               instancetype=instancetype, classtype=classtype)

    def append_field(self, field, value) -> Patch:
        """Generate patch appending a field initializer"""
        content = f'    .{field} = {value},\n'
        return Patch(self.match.end('fields'), self.match.end('fields'),
                     content)

    #def gen_patches(self) -> Iterable[Patch]:
    #    basic_fields = { 'parent', 'name',
    #                    'instance_size', 'instance_init', 'instance_finalize',
    #                    'class_size', 'class_init', }
    #    if fields == basic_fields:
    #        yield self.make_patch(f'// CAN PATCH: {self.name}\n')

class TypeInfoVarInitFuncs(TypeInfoVar):
    """TypeInfo variable
    Will create missing init functions
    """
    def gen_patches(self) -> Iterable[Patch]:
        values = self.initializers
        if values is None:
            self.warn("type not parsed completely: %s", self.name)
            return

        macro = self.file.find_match(TypeInfoVar, self.name)
        if macro is None:
            self.warn("No TYPE_INFO macro for %s", self.name)
            return

        ids = self.extract_identifiers()
        if ids is None:
            return

        DBG("identifiers extracted: %r", ids)
        fields = set(values.keys())
        if ids.lowercase:
            if 'instance_init' not in fields:
                yield self.prepend(('static void %s_init(Object *obj)\n'
                                    '{\n'
                                    '}\n\n') % (ids.lowercase))
                yield self.append_field('instance_init', ids.lowercase+'_init')

            if 'instance_finalize' not in fields:
                yield self.prepend(('static void %s_finalize(Object *obj)\n'
                                    '{\n'
                                    '}\n\n') % (ids.lowercase))
                yield self.append_field('instance_finalize', ids.lowercase+'_finalize')


            if 'class_init' not in fields:
                yield self.prepend(('static void %s_class_init(ObjectClass *oc, void *data)\n'
                                    '{\n'
                                    '}\n\n') % (ids.lowercase))
                yield self.append_field('class_init', ids.lowercase+'_class_init')

class TypeInitMacro(FileMatch):
    """type_info(...) macro use
    Will be deleted if function is empty
    """
    regexp = S(r'^[ \t]*type_init\(\s*', NAMED('name', RE_IDENTIFIER), r'\s*\);?[ \t]*\n')
    def gen_patches(self) -> Iterable[Patch]:
        fn = self.file.find_match(StaticVoidFunction, self.name)
        DBG("function for %s: %s", self.name, fn)
        if fn and fn.body == '':
            yield fn.make_patch('')
            yield self.make_patch('')

class StaticVoidFunction(FileMatch):
    """simple static void function
    (no replacement rules)
    """
    #NOTE: just like RE_FULL_STRUCT, this doesn't parse any of the body contents
    #      of the function.  Tt will just look for "}" in the beginning of a line
    regexp = S(r'static\s+void\s+', NAMED('name', RE_IDENTIFIER), r'\s*\(\s*void\s*\)\n',
               r'{\n',
               NAMED('body',
                     # acceptable inside the function body:
                     # - lines starting with space or tab
                     # - empty lines
                     # - preprocessor directives
                     r'([ \t][^\n]*\n|#[^\n]*\n|\n)*?'),
               r'}\n')

    @property
    def body(self) -> str:
        return self.group('body')

class TypeRegisterCall(FileMatch):
    """type_register_static() call
    Will be replaced by TYPE_INFO() macro
    """
    regexp = S(r'^[ \t]*type_register_static\(&\s*', NAMED('name', RE_IDENTIFIER), r'\s*\);[ \t]*\n')

    def function(self) -> Optional['StaticVoidFunction']:
        """Return function containing this call"""
        for m in self.file.matches_of_type(StaticVoidFunction):
            if m.contains(self):
                return m
        return None

    def gen_patches(self) -> Iterable[Patch]:
        fn = self.function()
        if fn is None:
            self.warn("can't find function where type_register_static(&%s) is called", self.name)
            return

        type_init = self.file.find_match(TypeInitMacro, fn.name)
        if type_init is None:
            self.warn("can't find type_init(%s) line", self.name)
            return

        var = self.file.find_match(TypeInfoVar, self.name)
        if var is None:
            self.warn("can't find TypeInfo var declaration for %s", self.name)
            return

        if not var.is_full():
            self.warn("variable declaration %s wasn't parsed fully", var.name)
            return

        if fn.contains(var):
            self.warn("TypeInfo %s variable is inside a function", self.name)
            return

        # delete type_register_static() call:
        yield self.make_patch('')
        # append TYPE_REGISTER(...) after variable declaration:
        yield var.append(f'TYPE_INFO({self.name})\n')

class TypeInfoMacro(FileMatch):
    """TYPE_INFO macro usage"""
    regexp = S(r'^[ \t]*TYPE_INFO\s*\(\s*', NAMED('name', RE_IDENTIFIER), r'\s*\)[ \t]*;?[ \t]*\n')

class CreateClassStruct(DeclareInstanceChecker):
    """Replace DECLARE_INSTANCE_CHECKER with OBJECT_DECLARE_SIMPLE_TYPE"""
    def gen_patches(self) -> Iterable[Patch]:
        typename = self.group('typename')
        DBG("looking for TypeInfo variable for %s", typename)
        ti = [ti for ti in self.allfiles.matches_of_type(TypeInfoVar)
              if ti.get_initializer_value('name').raw == typename]
        DBG("type info vars: %r", ti)
        if len(ti) > 1:
            self.warn("multiple TypeInfo vars found for %s", typename)
            return
        if len(ti) == 0:
            self.warn("no TypeInfo var found for %s", typename)
            return
        var = ti[0]
        assert var.initializers
        if 'class_size' in var.initializers:
            self.warn("class size already set for TypeInfo %s", var.name)
            return
        classtype = self.group('instancetype')+'Class'
        return
        yield
        #TODO: need to find out what's the parent class type...
        #yield var.append_field('class_size', f'sizeof({classtype})')
        #c = (f'OBJECT_DECLARE_SIMPLE_TYPE({instancetype}, {lowercase},\n'
        #     f'                           MODULE_OBJ_NAME, ParentClassType)\n')
        #yield self.make_patch(c)

def type_infos(file: FileInfo) -> Iterable[TypeInfoVar]:
    return file.matches_of_type(TypeInfoVar)

def full_types(file: FileInfo) -> Iterable[TypeInfoVar]:
    return [t for t in type_infos(file) if t.is_full()]

def partial_types(file: FileInfo) -> Iterable[TypeInfoVar]:
    return [t for t in type_infos(file) if not t.is_full()]
