import copy
import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
import chess
from torch.utils.data import DataLoader

# Quantization scales (must match parameters.h / nnue.h in the engine).
# QA: SCReLU clamp / feature-transformer scale (int16 accumulator).
# QB: FC1 dense scale (int8). Only FC1 is quantized; FC2/FC3 stay float.
QA = 255
QB = 64

def board_to_indices(board: chess.Board, perspective: chess.Color):
    def orient(sq):
        sq = sq if perspective == chess.WHITE else sq ^ 56
        if mirror:
            sq ^= 7
        return sq

    king_sq_raw = board.king(perspective)
    king_sq = king_sq_raw if perspective == chess.WHITE else king_sq_raw ^ 56
    mirror = chess.square_file(king_sq) >= 4

    if mirror:
        king_sq ^= 7

    king_rank = chess.square_rank(king_sq)
    king_file = chess.square_file(king_sq)

    king_bucket = king_rank * 4 + king_file
    indices = []

    for sq, piece in board.piece_map().items():
        if piece.piece_type == chess.KING and piece.color == perspective:
            continue

        if piece.piece_type == chess.KING:
            plane = 10
        elif piece.color == perspective:
            plane = piece.piece_type - 1
        else:
            plane = 4 + piece.piece_type

        idx = king_bucket * (11 * 64) + plane * 64 + orient(sq)
        indices.append(idx)

    us = perspective
    them = not perspective

    # Castling-right features. Under the horizontal mirror, kingside and queenside swap
    # (the h-side rook becomes the a-side rook), so the slot within each pair must swap too
    # (^1) to stay consistent with the mirrored piece features. nnue.h does the same
    # (get_castling_embedding_index applies slot ^= 1 when mirrored).
    castling_base = 32 * 11 * 64
    swap = 1 if mirror else 0

    if board.has_kingside_castling_rights(us):
        indices.append(castling_base + (0 ^ swap))

    if board.has_queenside_castling_rights(us):
        indices.append(castling_base + (1 ^ swap))

    if board.has_kingside_castling_rights(them):
        indices.append(castling_base + (2 ^ swap))

    if board.has_queenside_castling_rights(them):
        indices.append(castling_base + (3 ^ swap))

    return indices

class ChessDataset(torch.utils.data.Dataset):
    def __init__(self, fens_file="fens.npy", labels_file="labels.npy"):
        self.fens_file = fens_file
        self.labels_file = labels_file
        self._fens = None
        self._labels = None

        labels = np.load(labels_file, mmap_mode='r')
        self._len = len(labels)

    def _ensure_loaded(self):
        if self._fens is None:
            self._fens = np.load(self.fens_file, mmap_mode='r')
            self._labels = np.load(self.labels_file, mmap_mode='r')

    def __len__(self):
        return self._len

    def __getitem__(self, idx):
        self._ensure_loaded()

        fen_bytes = self._fens[idx]
        fen_str = fen_bytes.tobytes().decode("ascii").rstrip()

        board = chess.Board(fen_str)
        perspective = board.turn

        us_indices = board_to_indices(board, perspective)
        them_indices = board_to_indices(board, not perspective)
        
        label = self._labels[idx]

        return us_indices, them_indices, label

def collate_fn(batch):
    us_indices = []
    us_offsets = [0]
    us_total = 0

    them_indices = []
    them_offsets = [0]
    them_total = 0

    labels = []

    for us_idx, them_idx, label in batch:
        us_indices.extend(us_idx)
        us_total += len(us_idx)
        us_offsets.append(us_total)

        them_indices.extend(them_idx)
        them_total += len(them_idx)
        them_offsets.append(them_total)

        labels.append(label)

    return (
        torch.tensor(us_indices, dtype=torch.long),
        torch.tensor(us_offsets[:-1], dtype=torch.long),
        torch.tensor(them_indices, dtype=torch.long),
        torch.tensor(them_offsets[:-1], dtype=torch.long),
        torch.tensor(labels, dtype=torch.float32),
    )

class NNUE(nn.Module):
    def __init__(self, embedding_dim=512, hidden_dim1=32, hidden_dim2=32):
        super().__init__()

        self.embedding = nn.EmbeddingBag(32 * 11 * 64 + 4, embedding_dim, mode="sum")
        nn.init.normal_(self.embedding.weight, mean=0.0, std=0.01)

        self.fc1 = nn.Linear(embedding_dim * 2, hidden_dim1)
        self.fc2 = nn.Linear(hidden_dim1, hidden_dim2)
        self.fc3 = nn.Linear(hidden_dim2, 1)

        self.relu = nn.ReLU()

    def forward(self, us_idx, us_offsets, them_idx, them_offsets):
        us_x = self.embedding(us_idx, us_offsets)
        them_x = self.embedding(them_idx, them_offsets)

        us_x = torch.clamp(us_x, 0.0, 1.0).pow(2)
        them_x = torch.clamp(them_x, 0.0, 1.0).pow(2)

        x = torch.cat([us_x, them_x], dim=-1)
        x = self.relu(self.fc1(x))
        x = self.relu(self.fc2(x))
        x = self.fc3(x)

        return x.squeeze(-1)

def train():
    dataset = ChessDataset()
    n = len(dataset)

    rng = np.random.default_rng(seed=42)
    indices = rng.permutation(n)

    val_size = int(n * 0.005)
    train_indices = indices[val_size:]
    val_indices = indices[:val_size]

    train_dataset = torch.utils.data.Subset(dataset, train_indices)
    val_dataset = torch.utils.data.Subset(dataset, val_indices)

    train_loader = DataLoader(
        train_dataset,
        batch_size=8192,
        shuffle=True,
        collate_fn=collate_fn,
        num_workers=14,
        pin_memory=True,
        persistent_workers=True
    )

    val_loader = DataLoader(
        val_dataset,
        batch_size=8192,
        shuffle=False,
        collate_fn=collate_fn,
        num_workers=0,
        pin_memory=True,
    )

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = NNUE().to(device)
    criterion = nn.MSELoss()

    embedding_params = []
    dense_params = []
    for name, param in model.named_parameters():
        if 'embedding' in name:
            embedding_params.append(param)
        else:
            dense_params.append(param)

    optimizer = optim.AdamW([
        {'params': embedding_params, 'weight_decay': 0},
        {'params': dense_params, 'weight_decay': 1e-4}
    ], lr=3e-4)

    scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(optimizer, mode='min', factor=0.5, patience=3, min_lr=1e-6)

    best_val_loss = float('inf')
    epochs_no_improve = 0
    early_stop_patience = 5
    best_state = None

    print("Starting to train...")

    for epoch in range(500):
        model.train()
        train_loss = 0.0

        for i, (us_idx, us_off, them_idx, them_off, y) in enumerate(train_loader):
            us_idx = us_idx.to(device, non_blocking=True)
            us_off = us_off.to(device, non_blocking=True)
            them_idx = them_idx.to(device, non_blocking=True)
            them_off = them_off.to(device, non_blocking=True)
            y = y.to(device, non_blocking=True)

            optimizer.zero_grad()

            preds = model(us_idx, us_off, them_idx, them_off)
            loss = criterion(preds, y)

            loss.backward()
            optimizer.step()

            loss = loss.item()
            train_loss += loss

            if i % 1000 == 0 and i > 0:
                print(f"Iteration {i}, Loss: {loss:.8f}")

        train_loss /= len(train_loader)

        model.eval()
        val_loss = 0.0

        with torch.no_grad():
            for us_idx, us_off, them_idx, them_off, y in val_loader:
                us_idx = us_idx.to(device, non_blocking=True)
                us_off = us_off.to(device, non_blocking=True)
                them_idx = them_idx.to(device, non_blocking=True)
                them_off = them_off.to(device, non_blocking=True)
                y = y.to(device, non_blocking=True)

                preds = model(us_idx, us_off, them_idx, them_off)
                val_loss += criterion(preds, y).item()

        val_loss /= len(val_loader)
        scheduler.step(val_loss)

        print(f"Epoch {epoch}: Train Loss={train_loss:.8f}, Val Loss={val_loss:.8f}, LR={optimizer.param_groups[0]['lr']}")

        if val_loss < best_val_loss:
            best_val_loss = val_loss
            epochs_no_improve = 0
            best_state = copy.deepcopy(model.state_dict())

            params = {k: v.detach().cpu().numpy() for k, v in model.state_dict().items()}
            np.savez_compressed("nnue_params9.npz", **params)
        else:
            epochs_no_improve += 1
            if epochs_no_improve >= early_stop_patience:
                print("Early stopping triggered")
                break

    model.load_state_dict(best_state)
    model.eval()
    params = {k: v.detach().cpu().numpy() for k, v in model.state_dict().items()}
    np.savez_compressed("nnue_params9.npz", **params)

def compress():
    fens_list = []
    labels_list = []

    with open("labels.txt") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                fen, cp_str = line.split("\t")
                board = chess.Board(fen)
                cp = int(cp_str)
                if board.turn == chess.BLACK:
                    cp = -cp
                cp = np.tanh(cp / 400)
            except ValueError:
                continue
            fens_list.append(fen)
            labels_list.append(cp)

    print("Loaded data, now compressing...")

    fens_fixed = np.full((len(fens_list), 90), ord(' '), dtype=np.uint8)
    for i, fen in enumerate(fens_list):
        b = fen.encode("ascii")
        fens_fixed[i, :len(b)] = np.frombuffer(b, dtype=np.uint8)

    np.save("fens.npy", fens_fixed)
    np.save("labels.npy", np.array(labels_list, dtype=np.float32))

def convert():
    # Quantized export. Layout must match parameters.cpp:
    #   embedding int16, fc1.weight int8, fc1.bias int32, then fc2/fc3 float32.
    #
    # Training no longer clips fc1 weights. Instead, per fc1 output neuron o we
    # divide its weight row + bias by a factor s_o just large enough to bring the
    # row inside int8 range, and multiply the matching fc2 weight column (the one
    # that consumes h1[o]) by s_o. ReLU commutes with positive scaling
    # (relu(z / s) == relu(z) / s) and fc2 is stored as float32, so this
    # reparameterization is exact: the engine evaluates the identical function,
    # but fc1 now quantizes to int8 without saturating.
    data = np.load("nnue_params9.npz")

    emb = data["embedding.weight"].astype(np.float64)   # [EMBEDDINGS, EMBEDDING_DIM]
    fc1_w = data["fc1.weight"].astype(np.float64)       # [H1, INPUT]
    fc1_b = data["fc1.bias"].astype(np.float64)         # [H1]
    fc2_w = data["fc2.weight"].astype(np.float64)       # [H2, H1]
    fc2_b = data["fc2.bias"].astype(np.float64)         # [H2]

    # Per-neuron rescale so every fc1 weight lands in [-127, 127] after * QB.
    # s_o >= 1, so neurons that already fit are left untouched.
    limit = 127.0 / QB
    s = np.maximum(1.0, np.abs(fc1_w).max(axis=1) / limit)   # [H1]
    fc1_w /= s[:, None]
    fc1_b /= s
    fc2_w *= s[None, :]                                       # column o scales with h1[o]

    if s.max() > 1.0:
        n = int((s > 1.0).sum())
        print(f"Rescaled fc1: {n} neuron(s) folded into fc2, max factor {s.max():.4f}")

    fc1_q = np.round(fc1_w * QB)
    fc1_clip = int((np.abs(fc1_q) > 127).sum())
    if fc1_clip:
        print(f"WARNING: {fc1_clip} fc1 weights still saturate int8 after rescale")

    # Embedding int16 saturation. Individual rows are int16; the engine also sums
    # up to ~32 active rows into an int16 accumulator (nnue.h), so keep an eye on
    # this margin as the net/feature count grows — it can't be fixed by rescaling.
    emb_q = np.round(emb * QA)
    emb_clip = int((np.abs(emb_q) > 32767).sum())
    if emb_clip:
        print(f"WARNING: {emb_clip} embedding weights saturate int16 (accumulator headroom shrinking)")

    emb_q = np.clip(emb_q, -32768, 32767).astype("<i2")
    fc1_q = np.clip(fc1_q, -127, 127).astype("<i1")
    fc1_qb = np.round(fc1_b * (QA * QA) * QB).astype("<i4")

    with open("nnue_params9_q.bin", "wb") as f:
        f.write(emb_q.tobytes())
        f.write(fc1_q.tobytes())
        f.write(fc1_qb.tobytes())
        f.write(fc2_w.astype("<f4").tobytes())
        f.write(fc2_b.astype("<f4").tobytes())
        f.write(data["fc3.weight"].astype("<f4").tobytes())
        f.write(data["fc3.bias"].astype("<f4").tobytes())

if __name__ == "__main__":
    # compress()
    train()
    convert()