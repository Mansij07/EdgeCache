import { Kafka, logLevel } from 'kafkajs';
import { Pool } from 'pg';
import { loadConfig } from './config';
import { AccessEvent, RollupBuffer } from './rollup';

async function main() {
  const cfg = loadConfig();
  const pool = new Pool({ connectionString: cfg.databaseUrl, max: 4 });
  const buffer = new RollupBuffer();

  const kafka = new Kafka({
    clientId: 'edgecache-analytics',
    brokers: cfg.kafkaBrokers,
    logLevel: logLevel.ERROR,
    retry: { retries: 20, initialRetryTime: 1000 },
  });
  const consumer = kafka.consumer({ groupId: cfg.kafkaGroupId });

  await consumer.connect();
  await consumer.subscribe({ topic: cfg.kafkaTopic, fromBeginning: false });
  console.log(`[analytics] consuming ${cfg.kafkaTopic} from ${cfg.kafkaBrokers.join(',')}`);

  await consumer.run({
    eachMessage: async ({ message }) => {
      if (!message.value) return;
      try {
        const ev = JSON.parse(message.value.toString()) as AccessEvent;
        buffer.add(ev);
      } catch {
        // Malformed events are dropped — analytics is best-effort by design.
      }
    },
  });

  // Periodic flush of rolled-up deltas to Postgres.
  const timer = setInterval(async () => {
    try {
      const n = await buffer.flush(pool);
      if (n > 0) console.log(`[analytics] flushed ${n} rollup rows`);
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      console.error('[analytics] flush failed (will retry):', msg);
    }
  }, cfg.flushIntervalMs);

  const shutdown = async (sig: string) => {
    console.log(`[analytics] ${sig} received, shutting down`);
    clearInterval(timer);
    await consumer.disconnect().catch(() => undefined);
    await buffer.flush(pool).catch(() => undefined);
    await pool.end().catch(() => undefined);
    process.exit(0);
  };
  process.on('SIGINT', () => void shutdown('SIGINT'));
  process.on('SIGTERM', () => void shutdown('SIGTERM'));
}

main().catch((err) => {
  console.error('[analytics] fatal', err);
  process.exit(1);
});
