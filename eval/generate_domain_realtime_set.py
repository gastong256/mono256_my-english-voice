#!/usr/bin/env python3
"""Generate a constrained-domain ES->EN evaluation set for realtime meetings."""

from __future__ import annotations

import json
from pathlib import Path


OUTPUT_PATH = Path(__file__).with_name("domain_realtime_set.jsonl")


TECH_ITEMS = [
    {
        "topic_es": "PostgreSQL",
        "topic_en": "PostgreSQL",
        "kind_es": "la base de datos",
        "kind_en": "the database",
        "required_terms": ["PostgreSQL"],
    },
    {
        "topic_es": "Redis",
        "topic_en": "Redis",
        "kind_es": "el cache",
        "kind_en": "the cache",
        "required_terms": ["Redis"],
    },
    {
        "topic_es": "Kubernetes",
        "topic_en": "Kubernetes",
        "kind_es": "el cluster",
        "kind_en": "the cluster",
        "required_terms": ["Kubernetes"],
    },
    {
        "topic_es": "FastAPI",
        "topic_en": "FastAPI",
        "kind_es": "el servicio",
        "kind_en": "the service",
        "required_terms": ["FastAPI"],
    },
    {
        "topic_es": "Docker",
        "topic_en": "Docker",
        "kind_es": "el contenedor",
        "kind_en": "the container",
        "required_terms": ["Docker"],
    },
    {
        "topic_es": "Terraform",
        "topic_en": "Terraform",
        "kind_es": "la infraestructura",
        "kind_en": "the infrastructure",
        "required_terms": ["Terraform"],
    },
    {
        "topic_es": "RabbitMQ",
        "topic_en": "RabbitMQ",
        "kind_es": "la cola",
        "kind_en": "the queue",
        "required_terms": ["RabbitMQ"],
    },
    {
        "topic_es": "el load balancer",
        "topic_en": "the load balancer",
        "kind_es": "el balanceador",
        "kind_en": "the load balancer",
        "required_terms": ["load balancer"],
    },
]


SCENARIOS = [
    {
        "category": "architecture",
        "source": "necesitamos revisar {kind_es} de {topic_es} antes del release",
        "reference": "we need to review {kind_en} for {topic_en} before release",
    },
    {
        "category": "latency",
        "source": "hay alta latencia en {kind_es} de {topic_es} desde esta manana",
        "reference": "latency is high in {kind_en} for {topic_en} since this morning",
    },
    {
        "category": "migration",
        "source": "la migracion de {topic_es} no termino bien en produccion",
        "reference": "the {topic_en} migration did not finish well in production",
    },
    {
        "category": "deployment",
        "source": "vamos a desplegar {topic_es} hoy con poco margen",
        "reference": "we will deploy {topic_en} today with little margin",
    },
    {
        "category": "scaling",
        "source": "tenemos que escalar {topic_es} sin romper el slo",
        "reference": "we need to scale {topic_en} without breaking the SLO",
        "extra_terms": ["SLO"],
    },
    {
        "category": "queue",
        "source": "usemos {topic_es} como message queue temporal para el pico",
        "reference": "let's use {topic_en} as a temporary message queue for the spike",
        "extra_terms": ["message queue"],
    },
    {
        "category": "rate_limit",
        "source": "tenemos que poner rate limiting en {topic_es} esta semana",
        "reference": "we need rate limiting for {topic_en} this week",
        "extra_terms": ["rate limiting"],
    },
    {
        "category": "stability",
        "source": "quiero mantener {topic_es} estable durante la demo",
        "reference": "I want to keep {topic_en} stable during the demo",
    },
    {
        "category": "bottleneck",
        "source": "el cuello de botella ahora esta en {topic_es}",
        "reference": "the bottleneck is now in {topic_en}",
    },
    {
        "category": "rollback",
        "source": "si falla {topic_es} hacemos rollback rapido",
        "reference": "if {topic_en} fails we will do a fast rollback",
    },
]


def build_records() -> list[dict]:
    records: list[dict] = []
    counter = 1
    for tech in TECH_ITEMS:
        for scenario in SCENARIOS:
            required_terms = list(tech["required_terms"])
            required_terms.extend(scenario.get("extra_terms", []))
            records.append(
                {
                    "id": f"domain-{counter:03d}",
                    "category": scenario["category"],
                    "source_es": scenario["source"].format(**tech),
                    "reference_en": scenario["reference"].format(**tech),
                    "required_terms": required_terms,
                }
            )
            counter += 1

    # Add a second pass with shorter conversational variants to reach 320 examples.
    conversational_pairs = [
        (
            "podemos bajar la latencia de {topic_es} hoy",
            "we can reduce latency for {topic_en} today",
            "latency_short",
            ["latency"],
        ),
        (
            "quiero revisar {topic_es} con el equipo ahora",
            "I want to review {topic_en} with the team now",
            "review_short",
            [],
        ),
        (
            "no quiero romper {topic_es} en produccion",
            "I do not want to break {topic_en} in production",
            "prod_safety",
            [],
        ),
        (
            "necesitamos throughput mas estable en {topic_es}",
            "we need more stable throughput in {topic_en}",
            "throughput",
            ["throughput"],
        ),
        (
            "la demo depende de {topic_es} hoy",
            "the demo depends on {topic_en} today",
            "demo",
            [],
        ),
        (
            "si {topic_es} sigue lento cambiamos el plan",
            "if {topic_en} stays slow we will change the plan",
            "slow_path",
            [],
        ),
        (
            "quiero menos riesgo en {topic_es} antes del deploy",
            "I want less risk in {topic_en} before the deploy",
            "risk",
            [],
        ),
        (
            "hagamos una prueba corta de {topic_es} primero",
            "let's do a short test of {topic_en} first",
            "short_test",
            [],
        ),
        (
            "si {topic_es} aguanta el pico seguimos igual",
            "if {topic_en} handles the spike we will keep the same plan",
            "spike",
            [],
        ),
        (
            "quiero una salida simple para {topic_es}",
            "I want a simple solution for {topic_en}",
            "simple_solution",
            [],
        ),
        (
            "revisemos el rollback de {topic_es}",
            "let's review the rollback for {topic_en}",
            "rollback_short",
            [],
        ),
        (
            "el release depende de {topic_es} y del load balancer",
            "the release depends on {topic_en} and the load balancer",
            "release_dependency",
            ["load balancer"],
        ),
        (
            "si cambia {topic_es} tenemos que avisar al equipo",
            "if {topic_en} changes we need to tell the team",
            "change_notice",
            [],
        ),
        (
            "quiero menos tiempo de respuesta en {topic_es}",
            "I want less response time in {topic_en}",
            "response_time",
            [],
        ),
        (
            "dejemos {topic_es} listo para la migracion",
            "let's keep {topic_en} ready for the migration",
            "migration_ready",
            [],
        ),
        (
            "si {topic_es} falla usamos una cola temporal",
            "if {topic_en} fails we will use a temporary queue",
            "queue_fallback",
            ["queue"],
        ),
        (
            "quiero ver menos errores en {topic_es}",
            "I want to see fewer errors in {topic_en}",
            "errors",
            [],
        ),
        (
            "necesitamos una decision rapida sobre {topic_es}",
            "we need a fast decision about {topic_en}",
            "fast_decision",
            [],
        ),
        (
            "la prioridad hoy es estabilizar {topic_es}",
            "the priority today is to stabilize {topic_en}",
            "priority",
            [],
        ),
        (
            "mantengamos {topic_es} simple por ahora",
            "let's keep {topic_en} simple for now",
            "keep_simple",
            [],
        ),
        (
            "quiero confirmar el deploy de {topic_es}",
            "I want to confirm the deploy of {topic_en}",
            "confirm_deploy",
            [],
        ),
        (
            "revisemos la capacidad de {topic_es} ahora",
            "let's review the capacity of {topic_en} now",
            "capacity",
            [],
        ),
        (
            "si {topic_es} sube de costo cambiamos de estrategia",
            "if {topic_en} gets more expensive we will change strategy",
            "cost",
            [],
        ),
        (
            "necesitamos mas claridad sobre {topic_es}",
            "we need more clarity about {topic_en}",
            "clarity",
            [],
        ),
        (
            "quiero una version mas estable de {topic_es}",
            "I want a more stable version of {topic_en}",
            "stable_version",
            [],
        ),
        (
            "si {topic_es} mejora seguimos con el mismo diseno",
            "if {topic_en} improves we will keep the same design",
            "same_design",
            [],
        ),
        (
            "quiero revisar los logs de {topic_es}",
            "I want to review the logs for {topic_en}",
            "logs",
            [],
        ),
        (
            "necesitamos menos variacion en {topic_es}",
            "we need less variation in {topic_en}",
            "variation",
            [],
        ),
        (
            "pongamos un limite simple para {topic_es}",
            "let's add a simple limit for {topic_en}",
            "simple_limit",
            [],
        ),
        (
            "quiero una prueba final de {topic_es}",
            "I want a final test of {topic_en}",
            "final_test",
            [],
        ),
    ]

    for tech in TECH_ITEMS:
        for source, reference, category, extra_terms in conversational_pairs:
            records.append(
                {
                    "id": f"domain-{counter:03d}",
                    "category": category,
                    "source_es": source.format(**tech),
                    "reference_en": reference.format(**tech),
                    "required_terms": list(tech["required_terms"]) + list(extra_terms),
                }
            )
            counter += 1

    assert len(records) == 320, len(records)
    return records


def main() -> None:
    records = build_records()
    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    with OUTPUT_PATH.open("w", encoding="utf-8") as handle:
      for record in records:
        handle.write(json.dumps(record, ensure_ascii=True) + "\n")
    print(f"Wrote {len(records)} records to {OUTPUT_PATH}")


if __name__ == "__main__":
    main()
