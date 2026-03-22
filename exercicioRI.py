import math
from collections import defaultdict, Counter

# =====================================================================
# 1. BASE DE DADOS (Documentos da Atividade 08)
# =====================================================================
documentos = {
    "D1": "logan e ororo são x-men",
    "D2": "stark parker e logan já foram vingadores parker gostaria de ser novamente",
    "D3": "ororo e stark não são guardiões e sim vingadores groot e rocket são guardiões mas poderiam ser vingadores",
    "D4": "eu sou groot logan todos somos groot o groot irá ajudar ororo e os x-men",
    "D5": "rocket e groot formam uma boa dupla nos guardiões rocket é maluco mas adora o groot"
}

# Vocabulário restrito fornecido pelo enunciado da Atividade 08
vocabulario_permitido = {
    "logan", "ororo", "stark", "parker", "groot", 
    "rocket", "x-men", "vingadores", "guardiões"
}
vocabulario = sorted(list(vocabulario_permitido))
N = len(documentos) # Total de documentos (5)

# =====================================================================
# 2. PRÉ-PROCESSAMENTO (Filtrando palavras fora do vocabulário)
# =====================================================================
docs_tokenizados = {}
for doc_id, texto in documentos.items():
    # Removendo pontuação simples para evitar sujeira
    texto_limpo = texto.replace(',', '').replace('.', '')
    tokens = texto_limpo.lower().split()
    # Filtra mantendo APENAS as palavras que estão no vocabulário oficial
    tokens_filtrados = [t for t in tokens if t in vocabulario_permitido]
    docs_tokenizados[doc_id] = tokens_filtrados

# =====================================================================
# 3. PONDERAÇÃO TF (Term Frequency: 1 + log10(f_ij))
# =====================================================================
tf = defaultdict(dict)
for doc_id, tokens in docs_tokenizados.items():
    frequencias = Counter(tokens)
    for termo in vocabulario:
        f_ij = frequencias[termo]
        if f_ij > 0:
            tf[doc_id][termo] = 1 + math.log10(f_ij)
        else:
            tf[doc_id][termo] = 0.0

# =====================================================================
# 4. PONDERAÇÃO IDF (Inverse Document Frequency: log10(N / n_i))
# =====================================================================
idf = {}
for termo in vocabulario:
    n_i = sum(1 for tokens in docs_tokenizados.values() if termo in tokens)
    idf[termo] = math.log10(N / n_i) if n_i > 0 else 0.0

# =====================================================================
# 5. PONDERAÇÃO TF-IDF E NORMAS DOS VETORES
# =====================================================================
tfidf_docs = defaultdict(dict)
normas_docs = {}

for doc_id in documentos.keys():
    soma_quadrados = 0
    for termo in vocabulario:
        peso = tf[doc_id][termo] * idf[termo]
        tfidf_docs[doc_id][termo] = peso
        soma_quadrados += peso ** 2
    # Norma = raiz quadrada da soma dos quadrados dos pesos
    normas_docs[doc_id] = math.sqrt(soma_quadrados)

# =====================================================================
# 6. EXECUÇÃO DA CONSULTA E RANQUEAMENTO
# =====================================================================
def executar_consulta(consulta_str):
    print(f"\n{'='*50}")
    print(f"BUSCA: '{consulta_str}'")
    print(f"{'='*50}")
    
    # Filtra a consulta para usar apenas termos do vocabulário
    tokens_consulta = [t for t in consulta_str.lower().split() if t in vocabulario_permitido]
    freq_consulta = Counter(tokens_consulta)
    
    # TF-IDF da Consulta (assumindo a mesma fórmula para Q)
    tfidf_q = {}
    soma_quad_q = 0
    for termo in vocabulario:
        f_iq = freq_consulta[termo]
        tf_q = (1 + math.log10(f_iq)) if f_iq > 0 else 0.0
        
        peso_q = tf_q * idf[termo]
        tfidf_q[termo] = peso_q
        soma_quad_q += peso_q ** 2
        
    norma_q = math.sqrt(soma_quad_q)
    
    if norma_q == 0:
        print("Nenhum documento relevante encontrado.")
        return

    # Similaridade do Cosseno
    resultados = {}
    for doc_id in documentos.keys():
        produto_interno = sum(tfidf_docs[doc_id][t] * tfidf_q[t] for t in vocabulario)
        
        if normas_docs[doc_id] > 0:
            sim = produto_interno / (normas_docs[doc_id] * norma_q)
        else:
            sim = 0.0
            
        if sim > 0:
            resultados[doc_id] = sim

    # Ranqueamento
    ranking = sorted(resultados.items(), key=lambda x: x[1], reverse=True)
    
    print("Documentos ranqueados por relevância (Cosseno):")
    for pos, (doc_id, score) in enumerate(ranking, 1):
        print(f"{pos}º -> {doc_id} (Similaridade: {score:.4f})")

# =====================================================================
# 7. DEMONSTRANDO AS TRÊS BUSCAS DIFERENTES (Exigência do Professor)
# =====================================================================
if __name__ == "__main__":
    print("VOCABULÁRIO UTILIZADO:", vocabulario)
    
    # Busca 1: A busca exata que estava no enunciado teórico da Atv 08
    executar_consulta("logan ororo x-men")
    
    # Busca 2: Buscando pelos membros cósmicos
    executar_consulta("groot rocket guardiões")
    
    # Busca 3: Buscando pelos heróis da Terra
    executar_consulta("stark parker vingadores")