## tree-sitter-namumark
나무위키에서 사용되는 the seed 엔진의 문법 나무마크의 tree-sitter 증분 파서 구현체입니다.\
나무마크를 지원하는 vscode 확장 프로그램인 [나무코드](https://github.com/jhk1090/namucode)에서 사용하기 위해 제작되었습니다.

### 구현 목표
the seed 엔진의 문법 구현 방식을 최대한 따라가며, 암시적으로 오류가 될 수 있는 문법을 잡아내는 데에 목표를 두었습니다.\
tree-sitter가 사용하는 GLR 파서 알고리즘의 한계로 깨진 문법에 대한 백트래킹은 제한적으로만 구현되어 있어, 100% 정확한 파싱은 보증하지 않습니다.

### 사용 프로젝트
- vscode 확장 프로그램: [나무코드](https://github.com/jhk1090/namucode)

### 빌드
```sh
npm run build
```

### 참조한 문서 및 프로젝트
- https://tree-sitter.github.io/tree-sitter/
- https://github.com/wikimedia/tree-sitter-wikitext
- https://github.com/tree-sitter-grammars/tree-sitter-markdown
