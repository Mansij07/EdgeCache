export interface Config {
  kafkaBrokers: string[];
  kafkaTopic: string;
  kafkaGroupId: string;
  databaseUrl: string;
  flushIntervalMs: number;
}

export function loadConfig(): Config {
  return {
    kafkaBrokers: (process.env.KAFKA_BROKERS ?? 'kafka:9092').split(',').map((s) => s.trim()),
    kafkaTopic: process.env.KAFKA_TOPIC ?? 'edgecache.access-log',
    kafkaGroupId: process.env.KAFKA_GROUP_ID ?? 'edgecache-analytics',
    databaseUrl:
      process.env.DATABASE_URL ?? 'postgres://edgecache:edgecache@postgres:5432/edgecache',
    flushIntervalMs: parseInt(process.env.FLUSH_INTERVAL_MS ?? '5000', 10),
  };
}
