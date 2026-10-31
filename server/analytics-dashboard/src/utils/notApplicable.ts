import type { DashFilterKey } from './urlState'

/** A payload as the worker may send it: a worker older than the dashboard omits `not_applicable`. */
export type WireNotApplicable<T extends { not_applicable: object }> = Omit<T, 'not_applicable'> & {
  not_applicable?: Partial<T['not_applicable']>
}

/**
 * The payload with a `not_applicable` entry for every block in `blocks`: the
 * worker's value where it sent one, else null (every block reported).
 */
export function withNotApplicable<T extends object, K extends string>(
  payload: T & { not_applicable?: Partial<Record<K, DashFilterKey | null>> },
  blocks: readonly K[]
): T & { not_applicable: Record<K, DashFilterKey | null> } {
  const sent: Partial<Record<K, DashFilterKey | null>> = payload.not_applicable ?? {}
  const not_applicable = Object.fromEntries(blocks.map((b) => [b, sent[b] ?? null])) as Record<K, DashFilterKey | null>
  return { ...payload, not_applicable }
}
