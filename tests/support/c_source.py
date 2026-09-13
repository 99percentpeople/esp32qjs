"""Exact C function extraction used by native fixture assembly."""
def extract(source,name):
    import re
    literals=r'''"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|//[^\n]*|/\*[\s\S]*?\*/'''
    # A documentation comment may mention name() before the definition. Mask
    # literals/comments without changing offsets before matching declarations.
    masked=re.sub(literals,lambda m:re.sub(r'[^\n]',' ',m.group()),source)
    match=re.search(r'^[ \t]*[^;{}\n()]+\b'+re.escape(name)+r'\([^;{}]*\)\s*\{',masked,re.M)
    if not match:raise AssertionError(name)
    # A closing brace can share a line with a return statement. Looking for
    # '\n}\n' then silently includes following production functions twice.
    # Ignore braces in C strings, character literals and comments.
    tokens=literals+r'|[{}]'
    depth=1
    for token in re.finditer(tokens,source[match.end():]):
        if token.group()=='{':depth+=1
        elif token.group()=='}':
            depth-=1
            if not depth:return source[match.start():match.end()+token.end()]+'\n'
    raise AssertionError('unterminated function: '+name)
