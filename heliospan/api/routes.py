"""Read-only JSON API over the in-memory fleet."""

from fastapi import APIRouter, HTTPException, Request

from heliospan.models.domain import (
    Alert,
    DeviceDetail,
    FleetSnapshot,
    HealthStatus,
    RackDetail,
    RuleInfo,
    SiteCard,
    Summary,
)
from heliospan.services.rules import rule_catalog
from heliospan.services.store import FleetStore

router = APIRouter()


def _store(request: Request) -> FleetStore:
    return request.app.state.store


@router.get("/api/health", response_model=HealthStatus, tags=["meta"])
def health(request: Request) -> HealthStatus:
    store = _store(request)
    return HealthStatus(
        status="ok",
        tick=store.tick_index,
        telemetry_running=bool(request.app.state.run_simulator),
        devices=len(store.devices),
        racks=len(store.racks),
    )


@router.get("/api/fleet", response_model=FleetSnapshot, tags=["fleet"])
def fleet(request: Request) -> FleetSnapshot:
    return _store(request).snapshot()


@router.get("/api/summary", response_model=Summary, tags=["fleet"])
def summary(request: Request) -> Summary:
    return _store(request).snapshot().summary


@router.get("/api/sites/{site_id}", response_model=SiteCard, tags=["fleet"])
def site(site_id: str, request: Request) -> SiteCard:
    card = _store(request).site_card(site_id)
    if card is None:
        raise HTTPException(status_code=404, detail="Site not found")
    return card


@router.get("/api/racks/{rack_id}", response_model=RackDetail, tags=["fleet"])
def rack(rack_id: str, request: Request) -> RackDetail:
    detail = _store(request).rack_detail(rack_id)
    if detail is None:
        raise HTTPException(status_code=404, detail="Rack not found")
    return detail


@router.get("/api/devices/{device_id}", response_model=DeviceDetail, tags=["fleet"])
def device(device_id: str, request: Request) -> DeviceDetail:
    detail = _store(request).device_detail(device_id)
    if detail is None:
        raise HTTPException(status_code=404, detail="Device not found")
    return detail


@router.get("/api/alerts", response_model=list[Alert], tags=["alerts"])
def alerts(request: Request) -> list[Alert]:
    return _store(request).alerts


@router.get("/api/rules", response_model=list[RuleInfo], tags=["alerts"])
def rules() -> list[RuleInfo]:
    return rule_catalog()
